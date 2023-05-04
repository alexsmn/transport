#include "net/websocket_transport.h"

#include "net/transport_delegate_mock.h"

#include <boost/asio/io_context.hpp>
#include <gmock/gmock.h>

using namespace testing;

namespace net {

class WebSocketTransportTest : public Test {};

TEST_F(WebSocketTransportTest, Test) {
  boost::asio::io_context io_context;

  int port = 4000;

  MockTransportHandlers server_handlers;
  WebSocketTransport server{io_context, {}, port};
  ASSERT_EQ(OK, server.Open(server_handlers.AsHandlers()));
}

}  // namespace net
