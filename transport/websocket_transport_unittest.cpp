#include "transport/websocket_transport.h"

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/io_context.hpp>
#include <gmock/gmock.h>
#include <random>
#include <unordered_set>

using namespace testing;

namespace transport {

namespace {

inline int GenerateTestNetworkPort() {
  static std::mt19937 gen(std::random_device{}());
  static std::uniform_int_distribution distrib{30000, 40000};
  static std::unordered_set<int> seen;
  int port = distrib(gen);
  while (!seen.emplace(port).second) {
    port = distrib(gen);
  }
  return port;
}

}  // namespace

class WebSocketTransportTest : public Test {};

TEST_F(WebSocketTransportTest, Test) {
  boost::asio::io_context io_context;
  boost::asio::co_spawn(
      io_context,
      [&]() -> awaitable<void> {
        int port = GenerateTestNetworkPort();

        // `boost::asio::ip::make_address` doesn't support an empty string for
        // any IP address.
        WebSocketTransport server{io_context.get_executor(), "0.0.0.0", port};

        co_await server.open();

        io_context.stop();
      },
      boost::asio::detached);
  io_context.run();
}

}  // namespace transport
