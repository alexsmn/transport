#include "transport/transport.h"
#include "transport/message_reader_transport.h"
#include "transport/message_utils.h"
#include "transport/test/debug_logger.h"
#include "transport/test/test_message_reader.h"
#include "transport/transport_factory_impl.h"
#include "transport/transport_string.h"

#include <array>
#include <boost/asio/bind_executor.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/experimental/as_tuple.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/strand.hpp>
#include <future>
#include <gmock/gmock.h>
#include <ranges>

using namespace testing;

namespace transport {

struct TestParams {
  std::string transport_string;
  bool use_message_reader = false;
};

// The test parameter is a transport string with no active/passive parameter.
class TransportTest : public TestWithParam<TestParams> {
 public:
  TransportTest();

 protected:
  struct Server {
    [[nodiscard]] awaitable<Error> Init();

    [[nodiscard]] awaitable<Error> StartAccepting();
    [[nodiscard]] awaitable<Error> StartEchoing(any_transport transport);

    std::shared_ptr<const Logger> logger_;
    any_transport transport_;
  };

  struct Client {
    [[nodiscard]] awaitable<Error> Run();

    [[nodiscard]] awaitable<Error> StartReading();
    [[nodiscard]] awaitable<Error> Write(std::span<const char> data);
    [[nodiscard]] awaitable<Error> ExchangeMessage(
        std::span<const char> message);

    std::shared_ptr<const Logger> logger_;
    any_transport transport_;
  };

  virtual void SetUp() override;
  virtual void TearDown() override;

  any_transport CreateTransport(const Executor& executor,
                                const std::shared_ptr<const Logger>& logger,
                                bool active);

  Server MakeServer();

  boost::asio::io_context io_context_;

  TransportFactoryImpl transport_factory_{io_context_};

  std::vector<std::jthread> threads_;

  Server server_ = MakeServer();

  std::vector<std::unique_ptr<Client>> clients_;

  // Must be the latest member.
  boost::asio::executor_work_guard<boost::asio::io_context::executor_type>
      io_context_work_guard_ = boost::asio::make_work_guard(io_context_);

  static inline std::shared_ptr<Logger> kLogger =
      std::make_shared<DebugLogger>();

  static const int kThreadCount = 1;
  static const int kClientCount = 10;
  static const int kExchangeMessageCount = 3;

  // The message format must correspond to `TestMessageReader`.
  static constexpr const char kMessage[] = {3, 1, 2, 3};
};

INSTANTIATE_TEST_SUITE_P(
    AllTransportTests,
    TransportTest,
    // TODO: Random port.
    // TODO: Implement send retries and enable UDP tests: "UDP;Port=4322".
    testing::Values(TestParams{.transport_string = "TCP;Port=4321"},
                    TestParams{.transport_string = "TCP;Port=4321",
                               .use_message_reader = true}));

namespace {

[[nodiscard]] awaitable<Error> EchoData(any_transport transport) {
  std::vector<char> buffer;
  for (;;) {
    buffer.resize(64);
    auto bytes_read = co_await transport.read(buffer);
    if (!bytes_read.ok()) {
      co_return bytes_read.error();
    }
    buffer.resize(*bytes_read);

    if (auto bytes_written = co_await transport.write(buffer);
        !bytes_written.ok() || *bytes_written != buffer.size()) {
      co_return ERR_FAILED;
    }
  }

  co_return OK;
}

}  // namespace

// TransportTest::Server

awaitable<Error> TransportTest::Server::Init() {
  NET_CO_RETURN_IF_ERROR(co_await transport_.open());

  logger_->Write(LogSeverity::Normal, "Opened");

  boost::asio::co_spawn(transport_.get_executor(), StartAccepting(),
                        boost::asio::detached);

  co_return OK;
}

awaitable<Error> TransportTest::Server::StartAccepting() {
  for (;;) {
    logger_->Write(LogSeverity::Normal, "Accepting");

    NET_ASSIGN_OR_CO_RETURN(auto accepted_transport,
                            co_await transport_.accept());

    logger_->Write(LogSeverity::Normal, "Accepted");

    boost::asio::co_spawn(transport_.get_executor(),
                          StartEchoing(std::move(accepted_transport)),
                          boost::asio::detached);
  }

  logger_->Write(LogSeverity::Normal, "Closed");
  co_return OK;
}

awaitable<Error> TransportTest::Server::StartEchoing(any_transport transport) {
  auto open_result = co_await transport.open();

  if (open_result != OK) {
    logger_->Write(LogSeverity::Warning, "Connect error");
    co_return open_result;
  }

  co_return co_await EchoData(std::move(transport));

  logger_->Write(LogSeverity::Warning, "Disconnected");
}

// TransportTest::Client

awaitable<Error> TransportTest::Client::Run() {
  if (auto result = co_await transport_.open(); result != OK) {
    logger_->Write(LogSeverity::Error, "Failed to open the client transport");
    co_return result;
  }

  logger_->Write(LogSeverity::Normal, "Transport opened");

  for (int i = 0; i < kExchangeMessageCount; ++i) {
    if (auto result = co_await ExchangeMessage(kMessage); result != OK) {
      co_return result;
    }
  }
}

awaitable<Error> TransportTest::Client::ExchangeMessage(
    std::span<const char> message) {
  logger_->Write(LogSeverity::Normal, "Exchange message");

  if (auto result = co_await Write(message); result != OK) {
    co_return result;
  }

  std::vector<char> buffer;
  NET_CO_RETURN_IF_ERROR(co_await ReadMessage(transport_, 64, buffer));

  if (buffer.empty()) {
    logger_->Write(LogSeverity::Error,
                   "Connection closed gracefully while expecting echo");
    co_return ERR_CONNECTION_CLOSED;
  }

  if (!std::ranges::equal(buffer, kMessage)) {
    logger_->Write(LogSeverity::Error,
                   "Received message is not equal to the sent one.");
    co_return ERR_FAILED;
  }

  logger_->Write(LogSeverity::Normal, "Echo received");

  co_await transport_.close();

  co_return OK;
}

awaitable<Error> TransportTest::Client::StartReading() {
  std::array<char, std::size(kMessage)> data;

  if (auto bytes_read = co_await transport_.read(data);
      !bytes_read.ok() || *bytes_read != std::size(kMessage)) {
    logger_->WriteF(LogSeverity::Error, "Received message is too short");
    co_return ERR_FAILED;
  }

  if (!std::ranges::equal(data, kMessage)) {
    logger_->WriteF(LogSeverity::Error,
                    "Received message is not equal to the sent one");
    co_return ERR_FAILED;
  }

  if (auto result = co_await transport_.write(kMessage);
      !result.ok() || *result != std::size(kMessage)) {
    logger_->WriteF(LogSeverity::Error, "Failed to write echoed data");
    co_return ERR_FAILED;
  }

  co_return OK;
}

awaitable<Error> TransportTest::Client::Write(std::span<const char> data) {
  logger_->Write(LogSeverity::Normal, "Send message");

  auto result = co_await transport_.write(data);
  if (!result.ok()) {
    logger_->WriteF(LogSeverity::Error, "Error on writing echoed data: %s",
                    ErrorToShortString(result.error()).c_str());
    co_return result.error();
  }

  if (*result != data.size()) {
    logger_->Write(LogSeverity::Error, "Failed to write all echoed data");
    co_return ERR_FAILED;
  }

  co_return OK;
}

// TransportTest

TransportTest::TransportTest() {}

void TransportTest::SetUp() {
  for (int i = 0; i < kClientCount; ++i) {
    std::shared_ptr<Logger> logger =
        std::make_shared<ProxyLogger>(kLogger, std::format("Client {}", i + 1));

    auto transport =
        CreateTransport(boost::asio::make_strand(io_context_), logger,
                        /*active=*/true);

    clients_.emplace_back(
        std::make_unique<Client>(logger, std::move(transport)));
  }

  for (int i = 0; i < kThreadCount; ++i) {
    threads_.emplace_back([&io_context = io_context_] { io_context.run(); });
  }
}

void TransportTest::TearDown() {}

any_transport TransportTest::CreateTransport(
    const Executor& executor,
    const std::shared_ptr<const Logger>& logger,
    bool active) {
  auto transport_string_suffix = active ? ";Active" : ";Passive";

  auto transport = transport_factory_.CreateTransport(
      TransportString{GetParam().transport_string + transport_string_suffix},
      executor, logger);
  if (!transport.ok()) {
    throw std::runtime_error("Failed to create transport");
  }

  if (!GetParam().use_message_reader) {
    return std::move(*transport);
  }

  return any_transport{std::make_unique<MessageReaderTransport>(
      std::move(*transport), std::make_unique<TestMessageReader>(), logger)};
}

TransportTest::Server TransportTest::MakeServer() {
  std::shared_ptr<Logger> logger =
      std::make_shared<ProxyLogger>(kLogger, "Server");

  auto transport =
      CreateTransport(boost::asio::make_strand(io_context_), logger,
                      /*active=*/false);

  return Server{.logger_ = logger, .transport_ = std::move(transport)};
}

TEST_P(TransportTest, StressTest) {
  assert(clients_.size() == kClientCount);

  boost::asio::co_spawn(
      io_context_,
      [this]() -> awaitable<void> {
        EXPECT_EQ(co_await server_.Init(), OK);

        using OpType = decltype(boost::asio::co_spawn(
            io_context_, clients_[0]->Run(), boost::asio::deferred));

        std::vector<OpType> ops;
        for (auto& c : clients_) {
          ops.emplace_back(boost::asio::co_spawn(io_context_, c->Run(),
                                                 boost::asio::deferred));
        }

        auto [_a, _b, results] =
            co_await boost::asio::experimental::make_parallel_group(
                std::move(ops))
                .async_wait(boost::asio::experimental::wait_for_one_error{},
                            boost::asio::use_awaitable);

        io_context_.stop();
      },
      boost::asio::detached);

  io_context_.run();
}

}  // namespace transport
