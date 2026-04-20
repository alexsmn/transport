#include "transport/websocket_transport.h"

#include "transport/test/coroutine_util.h"
#include "transport/test/test_log.h"

#include <boost/asio/io_context.hpp>
#include <boost/asio/use_future.hpp>
#include <gmock/gmock.h>
#include <random>
#include <unordered_set>

namespace transport {
namespace {

int GenerateTestNetworkPort() {
  static std::mt19937 gen(std::random_device{}());
  static std::uniform_int_distribution distrib{30000, 40000};
  static std::unordered_set<int> seen;
  int port = distrib(gen);
  while (!seen.emplace(port).second) {
    port = distrib(gen);
  }
  return port;
}

log_source MakeTestLog() {
  return log_source{std::make_shared<TestLogSink>()};
}

TEST(WebSocketTransportTest, ActiveAndPassiveExchangeMessages) {
  boost::asio::io_context io_context;
  const auto port = std::to_string(GenerateTestNetworkPort());
  auto log = MakeTestLog();

  auto future = boost::asio::co_spawn(io_context, [&]() -> awaitable<void> {
    WebSocketTransport server{
        io_context.get_executor(), log.with_channel("Server"), "127.0.0.1",
        port, /*active=*/false};
    WebSocketTransport client{
        io_context.get_executor(), log.with_channel("Client"), "127.0.0.1",
        port, /*active=*/true};

    NET_EXPECT_OK(co_await server.open());
    NET_EXPECT_OK(co_await client.open());

    auto accepted_result = co_await server.accept();
    EXPECT_TRUE(accepted_result.ok());
    if (!accepted_result.ok())
      co_return;
    auto accepted = std::move(*accepted_result);
    EXPECT_TRUE(accepted.connected());
    EXPECT_EQ(accepted.name(), "WebSocket Connection");

    constexpr std::array<char, 4> kRequest = {3, 1, 2, 3};
    auto written_result = co_await client.write(kRequest);
    EXPECT_TRUE(written_result.ok());
    if (!written_result.ok())
      co_return;
    auto written = *written_result;
    EXPECT_EQ(written, kRequest.size());

    std::array<char, 16> read_buffer{};
    auto accepted_read_result = co_await accepted.read(read_buffer);
    EXPECT_TRUE(accepted_read_result.ok());
    if (!accepted_read_result.ok())
      co_return;
    auto accepted_read = *accepted_read_result;
    EXPECT_EQ(accepted_read, kRequest.size());
    EXPECT_TRUE(std::equal(
        kRequest.begin(), kRequest.end(), read_buffer.begin(),
        read_buffer.begin() + static_cast<std::ptrdiff_t>(accepted_read)));

    auto echoed_result = co_await accepted.write(
        std::span<const char>{read_buffer.data(), accepted_read});
    EXPECT_TRUE(echoed_result.ok());
    if (!echoed_result.ok())
      co_return;
    auto echoed = *echoed_result;
    EXPECT_EQ(echoed, accepted_read);

    std::array<char, 16> client_read_buffer{};
    auto client_read_result = co_await client.read(client_read_buffer);
    EXPECT_TRUE(client_read_result.ok());
    if (!client_read_result.ok())
      co_return;
    auto client_read = *client_read_result;
    EXPECT_EQ(client_read, kRequest.size());
    EXPECT_TRUE(std::equal(
        kRequest.begin(), kRequest.end(), client_read_buffer.begin(),
        client_read_buffer.begin() + static_cast<std::ptrdiff_t>(client_read)));

    NET_EXPECT_OK(co_await server.close());
  }, boost::asio::use_future);

  io_context.run();
  future.get();
}

TEST(WebSocketTransportTest, AcceptedTransportIsMessageOrientedAndPassive) {
  boost::asio::io_context io_context;
  const auto port = std::to_string(GenerateTestNetworkPort());
  auto log = MakeTestLog();

  auto future = boost::asio::co_spawn(io_context, [&]() -> awaitable<void> {
    WebSocketTransport server{
        io_context.get_executor(), log.with_channel("Server"), "127.0.0.1",
        port, /*active=*/false};
    WebSocketTransport client{
        io_context.get_executor(), log.with_channel("Client"), "127.0.0.1",
        port, /*active=*/true};

    EXPECT_FALSE(server.active());
    EXPECT_TRUE(client.active());
    EXPECT_TRUE(server.message_oriented());
    EXPECT_TRUE(client.message_oriented());

    NET_EXPECT_OK(co_await server.open());
    NET_EXPECT_OK(co_await client.open());

    auto accepted_result = co_await server.accept();
    EXPECT_TRUE(accepted_result.ok());
    if (!accepted_result.ok())
      co_return;
    auto accepted = std::move(*accepted_result);
    EXPECT_FALSE(accepted.active());
    EXPECT_TRUE(accepted.message_oriented());
    EXPECT_TRUE(accepted.connected());

    NET_EXPECT_OK(co_await server.close());
  }, boost::asio::use_future);

  io_context.run();
  future.get();
}

}  // namespace
}  // namespace transport
