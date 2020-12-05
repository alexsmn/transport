#include "net/udp_transport.h"

#include "net/transport_delegate_mock.h"
#include "net/udp_socket.h"

#include <gmock/gmock.h>
#include <optional>

namespace net {

using namespace testing;

namespace {

class MockUdpSocket : public UdpSocket {
 public:
  MOCK_METHOD0(Open, void());
  MOCK_METHOD0(Close, void());
  MOCK_METHOD2(SendTo, void(const Endpoint& endpoint, Datagram&& datagram));
};

}  // namespace

class AsioUdpTransportTest : public Test {
 public:
  virtual void SetUp() override;

  boost::asio::io_context io_context;

  std::shared_ptr<MockUdpSocket> socket = std::make_shared<MockUdpSocket>();
  UdpSocketContext::OpenHandler open_handler;
  UdpSocketContext::MessageHandler message_handler;

  UdpSocketFactory udp_socket_factory = [&](UdpSocketContext&& context) {
    open_handler = std::move(context.open_handler_);
    message_handler = std::move(context.message_handler_);
    return socket;
  };

  TransportDelegateMock delegate;
  AsioUdpTransport transport{io_context, udp_socket_factory};

  TransportDelegateMock accepted_delegate;
  std::unique_ptr<Transport> accepted_transport;
};

void AsioUdpTransportTest::SetUp() {
  transport.active = false;

  EXPECT_CALL(*socket, Open());
  ASSERT_EQ(OK, transport.Open(delegate));
  EXPECT_FALSE(transport.IsActive());
  EXPECT_FALSE(transport.IsConnected());

  const UdpSocket::Endpoint endpoint;
  EXPECT_CALL(delegate, OnTransportOpened());
  open_handler(endpoint);
  EXPECT_TRUE(transport.IsConnected());

  const UdpSocket::Endpoint peer_endpoint;
  EXPECT_CALL(accepted_delegate, OnTransportMessageReceived(_, _));
  EXPECT_CALL(delegate, OnTransportAccepted(_))
      .WillOnce(Invoke([&](std::unique_ptr<Transport> t) {
        t->Open(accepted_delegate);
        accepted_transport = std::move(t);
        return net::OK;
      }));
  UdpSocket::Datagram datagram;
  message_handler(peer_endpoint, std::move(datagram));
  ASSERT_TRUE(accepted_transport);
  EXPECT_TRUE(accepted_transport->IsConnected());
}

TEST_F(AsioUdpTransportTest, AcceptedTransportDestroyed) {
  accepted_transport.reset();

  EXPECT_CALL(*socket, Close());
}

TEST_F(AsioUdpTransportTest, AcceptedTransportClosed) {
  accepted_transport->Close();
  EXPECT_FALSE(accepted_transport->IsConnected());

  EXPECT_CALL(*socket, Close());
}

}  // namespace net
