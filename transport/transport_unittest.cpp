#include "transport/transport.h"
#include "transport/message_reader_transport.h"
#include "transport/message_utils.h"
#include "transport/test/coroutine_util.h"
#include "transport/test/test_log.h"
#include "transport/test/test_message_reader.h"
#include "transport/transport_factory_impl.h"
#include "transport/transport_string.h"

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/strand.hpp>
#include <gmock/gmock.h>
#include <thread>

using namespace testing;

namespace transport {

struct TestParams {
  std::string transport_string;
  int thread_count = 10;
};

// The test parameter is a transport string with no active/passive parameter.
class TransportTest : public TestWithParam<TestParams> {
 public:
  TransportTest();

 protected:
  virtual void SetUp() override;
  virtual void TearDown() override;

  any_transport CreateTransport(const Executor& executor,
                                const log_source& log,
                                bool active);

  boost::asio::io_context io_context_{GetParam().thread_count};

  TransportFactoryImpl transport_factory_{io_context_};

  static inline log_source kLog{std::make_shared<TestLogSink>()};

  static const int kClientCount = 100;
  static const int kClientExchangeCount = 3;

  // The message format must correspond to `TestMessageReader`.
  static constexpr const char kClientExchangeMessage[] = {3, 1, 2, 3};
};

INSTANTIATE_TEST_SUITE_P(
    AllTransportTests,
    TransportTest,
    // TODO: Random port.
    // TODO: Enable multi-threaded UDP tests.
    testing::Values(TestParams{.transport_string = "TCP;Port=4321"},
                    TestParams{.transport_string = "TCP;Port=4322"},
                    TestParams{.transport_string = "UDP;Port=4323"}));

namespace {

[[nodiscard]] awaitable<Error> Write(const any_transport& transport,
                                     std::span<const char> data) {
  NET_ASSIGN_OR_CO_RETURN(auto bytes_written, co_await transport.write(data));

  if (bytes_written != data.size()) {
    co_return ERR_FAILED;
  }

  co_return OK;
}

[[nodiscard]] awaitable<Error> RunEchoAccepted(any_transport transport) {
  auto cancelation = co_await boost::asio::this_coro::cancellation_state;
  std::vector<char> buffer;
  for (;;) {
    NET_CO_RETURN_IF_ERROR(
        co_await ReadMessage(transport, /*max_size=*/64, buffer));

    if (cancelation.cancelled() != boost::asio::cancellation_type::none) {
      co_return ERR_ABORTED;
    }

    if (buffer.empty()) {
      co_return OK;
    }

    NET_ASSIGN_OR_CO_RETURN(auto bytes_written,
                            co_await transport.write(buffer));

    if (cancelation.cancelled() != boost::asio::cancellation_type::none) {
      co_return ERR_ABORTED;
    }

    if (bytes_written != buffer.size()) {
      co_return ERR_FAILED;
    }
  }

  co_return co_await transport.close();
}

[[nodiscard]] awaitable<Error> RunServer(any_transport& transport) {
  int next_client_id = 1;

  for (;;) {
    NET_ASSIGN_OR_CO_RETURN(auto accepted_transport,
                            co_await transport.accept());

    boost::asio::co_spawn(transport.get_executor(),
                          RunEchoAccepted(std::move(accepted_transport)),
                          boost::asio::detached);
  }

  co_return OK;
}

[[nodiscard]] awaitable<Error> RunEchoClient(
    any_transport transport,
    std::span<const char> exchange_message,
    int exchange_count) {
  NET_CO_RETURN_IF_ERROR(co_await transport.open());

  std::vector<char> buffer;
  for (int i = 0; i < exchange_count; ++i) {
    NET_CO_RETURN_IF_ERROR(co_await Write(transport, exchange_message));
    NET_CO_RETURN_IF_ERROR(
        co_await ReadMessage(transport, /*max_size=*/64, buffer));

    if (buffer.empty()) {
      co_return ERR_CONNECTION_CLOSED;
    }

    if (!std::ranges::equal(buffer, exchange_message)) {
      co_return ERR_FAILED;
    }
  }

  co_return co_await transport.close();
}

}  // namespace

// TransportTest

TransportTest::TransportTest() {}

void TransportTest::SetUp() {}

void TransportTest::TearDown() {}

any_transport TransportTest::CreateTransport(const Executor& executor,
                                             const log_source& log,
                                             bool active) {
  auto transport_string_suffix = active ? ";Active" : ";Passive";

  auto transport = transport_factory_.CreateTransport(
      TransportString{GetParam().transport_string + transport_string_suffix},
      executor, log);

  if (!transport.ok()) {
    throw std::runtime_error("Failed to create transport");
  }

  if (transport->message_oriented()) {
    return std::move(*transport);
  }

  return BindMessageReader(std::move(*transport),
                           std::make_unique<TestMessageReader>(), log);
}

TEST_P(TransportTest, StressTest) {
  boost::asio::co_spawn(
      io_context_,
      [this]() -> awaitable<void> {
        auto server = CreateTransport(boost::asio::make_strand(io_context_),
                                      kLog.with_channel("Server"),
                                      /*active=*/false);
        NET_EXPECT_OK(co_await server.open());
        boost::asio::co_spawn(server.get_executor(), RunServer(server),
                              boost::asio::detached);

        using OpType = decltype(boost::asio::co_spawn(
            io_context_, RunEchoClient(any_transport{}, {}, 1),
            boost::asio::deferred));

        std::vector<OpType> ops;

        for (int i = 0; i < kClientCount; ++i) {
          auto client = CreateTransport(
              boost::asio::make_strand(io_context_),
              kLog.with_channel("Client " + std::to_string(i + 1)),
              /*active=*/true);
          auto executor = client.get_executor();
          ops.emplace_back(boost::asio::co_spawn(
              executor,
              RunEchoClient(std::move(client), kClientExchangeMessage,
                            kClientExchangeCount),
              boost::asio::deferred));
        }

        auto [_a, _b, results] =
            co_await boost::asio::experimental::make_parallel_group(
                std::move(ops))
                .async_wait(boost::asio::experimental::wait_for_one_error{},
                            boost::asio::use_awaitable);

        for (const auto& result : results) {
          EXPECT_EQ(result, OK);
        }

        NET_EXPECT_OK(co_await server.close());
        io_context_.stop();
      },
      boost::asio::detached);

  std::vector<std::jthread> threads;
  for (int i = 0; i < GetParam().thread_count - 1; ++i) {
    threads.emplace_back([&io_context = io_context_] { io_context.run(); });
  }
  io_context_.run();
}

}  // namespace transport
