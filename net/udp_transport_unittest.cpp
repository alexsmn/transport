#include "net/udp_transport.h"

#include "net/logger.h"
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

  void OpenTransport(bool active);
  void ReceiveMessage();

  void ExpectTransportAccepted();

  std::shared_ptr<MockUdpSocket> socket = std::make_shared<MockUdpSocket>();
  UdpSocketContext::OpenHandler open_handler;
  UdpSocketContext::MessageHandler message_handler;

  UdpSocketFactory udp_socket_factory = [&](UdpSocketContext&& context) {
    open_handler = std::move(context.open_handler_);
    message_handler = std::move(context.message_handler_);
    return socket;
  };

  TransportDelegateMock delegate;
  std::unique_ptr<Transport> transport_;

  TransportDelegateMock accepted_delegate;
  std::unique_ptr<Transport> accepted_transport;
};

void AsioUdpTransportTest::SetUp() {}

void AsioUdpTransportTest::OpenTransport(bool active) {
  transport_ = std::make_unique<AsioUdpTransport>(NullLogger::GetInstance(),
                                                  udp_socket_factory,
                                                  /*host*/ std::string{},
                                                  /*service*/ std::string{},
                                                  /*active*/ active);

  EXPECT_CALL(*socket, Open());
  ASSERT_EQ(OK, transport_->Open(delegate));
  EXPECT_FALSE(transport_->IsActive());
  EXPECT_FALSE(transport_->IsConnected());

  const UdpSocket::Endpoint endpoint;
  EXPECT_CALL(delegate, OnTransportOpened());
  open_handler(endpoint);
  EXPECT_TRUE(transport_->IsConnected());
}

void AsioUdpTransportTest::ReceiveMessage() {
  const UdpSocket::Endpoint peer_endpoint;
  UdpSocket::Datagram datagram;
  message_handler(peer_endpoint, std::move(datagram));
}

void AsioUdpTransportTest::ExpectTransportAccepted() {
  EXPECT_CALL(delegate, OnTransportAccepted(_))
      .WillOnce(Invoke([&](std::unique_ptr<Transport> t) {
        t->Open(accepted_delegate);
        accepted_transport = std::move(t);
        return net::OK;
      }));
}

TEST_F(AsioUdpTransportTest, UdpServer_AcceptedTransportIgnored) {
  OpenTransport(false);

  EXPECT_CALL(delegate, OnTransportAccepted(_))
      .WillOnce(Invoke([&](std::unique_ptr<Transport> t) {
        // Ignore accepted transport_->
        return net::OK;
      }));

  ReceiveMessage();

  EXPECT_CALL(*socket, Close());
}

TEST_F(AsioUdpTransportTest, UdpServer_AcceptedTransportDestroyed) {
  OpenTransport(false);
  ExpectTransportAccepted();

  EXPECT_CALL(accepted_delegate, OnTransportMessageReceived(_, _));
  ReceiveMessage();

  ASSERT_TRUE(accepted_transport);
  EXPECT_TRUE(accepted_transport->IsConnected());

  accepted_transport.reset();

  EXPECT_CALL(*socket, Close());
}

TEST_F(AsioUdpTransportTest, UdpServer_AcceptedTransportClosed) {
  OpenTransport(false);
  ExpectTransportAccepted();

  EXPECT_CALL(accepted_delegate, OnTransportMessageReceived(_, _));
  ReceiveMessage();

  ASSERT_TRUE(accepted_transport);
  EXPECT_TRUE(accepted_transport->IsConnected());

  accepted_transport->Close();
  EXPECT_FALSE(accepted_transport->IsConnected());

  EXPECT_CALL(*socket, Close());
}

TEST_F(AsioUdpTransportTest,
       UdpServer_AcceptedTransportDestroyedFromMessageHandler) {
  OpenTransport(false);
  ExpectTransportAccepted();

  EXPECT_CALL(accepted_delegate, OnTransportMessageReceived(_, _))
      .WillOnce(Invoke([&] { accepted_transport.reset(); }));
  ReceiveMessage();

  EXPECT_CALL(*socket, Close());
}

TEST_F(AsioUdpTransportTest,
       UdpServer_AcceptedTransportClosedFromMessageHandler) {
  OpenTransport(false);
  ExpectTransportAccepted();

  EXPECT_CALL(accepted_delegate, OnTransportMessageReceived(_, _))
      .WillOnce(Invoke([&] { accepted_transport->Close(); }));
  ReceiveMessage();

  EXPECT_CALL(*socket, Close());
}

}  // namespace net
