#include "net/transport.h"
#include "net/test/debug_logger.h"
#include "net/transport_factory_impl.h"
#include "net/transport_string.h"

#include <array>
#include <boost/asio/bind_executor.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>
#include <boost/asio/experimental/channel.hpp>
#include <boost/asio/experimental/promise.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/strand.hpp>
#include <future>
#include <gmock/gmock.h>
#include <ranges>

using namespace testing;

namespace net {

// The test parameter is a transport string with no active/passive parameter.
class TransportTest : public TestWithParam<std::string /*transport_string*/> {
 public:
  TransportTest();

 protected:
  virtual void SetUp() override;
  virtual void TearDown() override;

  boost::asio::io_context io_context_;

  TransportFactoryImpl transport_factory_{io_context_};

  struct Server {
    [[nodiscard]] boost::asio::awaitable<void> Init();

    [[nodiscard]] boost::asio::awaitable<void> StartEchoing(
        std::unique_ptr<Transport> transport);

    Executor executor_;
    TransportFactory& transport_factory_;

    std::shared_ptr<Logger> logger_ =
        std::make_shared<ProxyLogger>(kLogger, "Server");

    std::unique_ptr<Transport> transport_ = transport_factory_.CreateTransport(
        TransportString{GetParam() + ";Passive"},
        executor_,
        logger_);
  };

  struct Client {
    [[nodiscard]] boost::asio::awaitable<void> Run();

    std::string name_;
    Executor executor_;
    TransportFactory& transport_factory_;

    std::shared_ptr<Logger> logger_ =
        std::make_shared<ProxyLogger>(kLogger, name_.c_str());

    std::unique_ptr<Transport> transport_ = transport_factory_.CreateTransport(
        TransportString{GetParam() + ";Active"},
        executor_,
        logger_);
  };

  std::vector<std::jthread> threads_;

  Server server_{boost::asio::make_strand(io_context_), transport_factory_};
  std::vector<std::unique_ptr<Client>> clients_;

  // Must be the latest member.
  boost::asio::executor_work_guard<boost::asio::io_context::executor_type>
      io_context_work_guard_ = boost::asio::make_work_guard(io_context_);

  static inline std::shared_ptr<Logger> kLogger =
      std::make_shared<DebugLogger>();

  static const int kThreadCount = 10;
  static const int kClientCount = 100;
  static constexpr const char kMessage[] = "Message";
};

INSTANTIATE_TEST_SUITE_P(AllTransportTests,
                         TransportTest,
                         // TODO: Random port.
                         // TODO: Implement send retries and enable UDP tests.
                         testing::Values("TCP;Port=4321" /*,
                                         "UDP;Port=4322"*/));

namespace {

[[nodiscard]] boost::asio::awaitable<void> EchoData(Transport& transport) {
  for (;;) {
    std::vector<char> buffer(64);
    int res = transport.Read(buffer);
    if (res <= 0) {
      break;
    }
    co_await transport.Write(std::move(buffer));
  }
}

}  // namespace

// TransportTest::Server

boost::asio::awaitable<void> TransportTest::Server::Init() {
  return transport_->Open(
      {.on_accept = [this](std::unique_ptr<Transport> accepted_transport) {
        logger_->Write(LogSeverity::Normal, "Connected");
        boost::asio::co_spawn(executor_,
                              StartEchoing(std::move(accepted_transport)),
                              boost::asio::detached);
      }});
}

boost::asio::awaitable<void> TransportTest::Server::StartEchoing(
    std::unique_ptr<Transport> transport) {
  co_await transport->Open(
      {.on_close =
           [this, &transport](net::Error) mutable {
             logger_->Write(LogSeverity::Warning, "Disconnected");
             transport.reset();
           },
       .on_data =
           [this, &transport] {
             logger_->Write(LogSeverity::Normal, "Data received. Send echo");
             boost::asio::co_spawn(executor_, EchoData(*transport),
                                   boost::asio::detached);
           },
       .on_message =
           [this, &transport](std::span<const char> data) {
             logger_->Write(LogSeverity::Normal, "Message received. Send echo");
             boost::asio::co_spawn(
                 executor_,
                 transport->Write(std::vector<char>{data.begin(), data.end()}),
                 boost::asio::detached);
           }});
}

// TransportTest::Client

boost::asio::awaitable<void> TransportTest::Client::Run() {
  boost::asio::experimental::channel<void()> received_message{executor_};

  co_await transport_->Open(
      {.on_close =
           [this, &received_message](Error error) {
             logger_->Write(LogSeverity::Normal, "Closed");
             received_message.try_send();
           },
       .on_data =
           [this, &received_message] {
             logger_->Write(LogSeverity::Normal, "Message received");
             std::array<char, std::size(kMessage)> data;
             if (transport_->Read(data) != std::size(kMessage)) {
               throw std::runtime_error{"Received message is too short"};
             }
             if (!std::ranges::equal(data, kMessage)) {
               throw std::runtime_error{
                   "Received message is not equal to the sent one."};
             }
             received_message.try_send();
           },
       .on_message =
           [this, &received_message](std::span<const char> data) {
             logger_->Write(LogSeverity::Normal, "Message received. Send echo");
             boost::asio::co_spawn(
                 executor_,
                 transport_->Write(std::vector<char>{data.begin(), data.end()}),
                 boost::asio::detached);
             if (!std::ranges::equal(data, kMessage)) {
               throw std::runtime_error{
                   "Received message is not equal to the sent one."};
             }
             received_message.try_send();
           }});

  logger_->Write(LogSeverity::Normal, "Send message");

  co_await transport_->Write(
      std::vector<char>{std::begin(kMessage), std::end(kMessage)});

  transport_->Close();
}

// TransportTest

TransportTest::TransportTest() {}

void TransportTest::SetUp() {
  for (int i = 0; i < kClientCount; ++i) {
    clients_.emplace_back(std::make_unique<Client>(
        /*name=*/std::format("Client {}", i + 1),
        /*executor=*/boost::asio::make_strand(io_context_),
        transport_factory_));
  }

  for (int i = 0; i < kThreadCount; ++i) {
    threads_.emplace_back([&io_context = io_context_] { io_context.run(); });
  }
}

void TransportTest::TearDown() {}

TEST_P(TransportTest, StressTest) {
  boost::asio::co_spawn(
      io_context_,
      [this]() -> boost::asio::awaitable<void> {
        // Connect all.
        co_await server_.Init();

        using OpType = decltype(boost::asio::co_spawn(
            io_context_, clients_[0]->Run(), boost::asio::deferred));

        std::vector<OpType> ops;
        for (auto& c : clients_) {
          ops.emplace_back(boost::asio::co_spawn(io_context_, c->Run(),
                                                 boost::asio::deferred));
        }
        co_await boost::asio::experimental::make_parallel_group(
            std::move(ops))
            .async_wait(boost::asio::experimental::wait_for_one_error{},
                        boost::asio::use_awaitable);
      },
      boost::asio::detached);
}

}  // namespace net
