#include "net/udp_transport.h"

#include "net/logger.h"
#include "net/test/coroutine_util.h"
#include "net/transport_delegate_mock.h"
#include "net/udp_socket.h"

#include <gmock/gmock.h>
#include <optional>

namespace net {

using namespace testing;

namespace {

class MockUdpSocket : public UdpSocket {
 public:
  MockUdpSocket() {
    ON_CALL(*this, Open()).WillByDefault(CoReturn(ERR_FAILED));
    ON_CALL(*this, Close()).WillByDefault(CoReturnVoid());

    ON_CALL(*this, SendTo(/*endpoint=*/_, /*datagram=*/_))
        .WillByDefault(CoReturn(ErrorOr<size_t>(static_cast<size_t>(0))));
  }

  MOCK_METHOD(awaitable<net::Error>, Open, (), (override));
  MOCK_METHOD(awaitable<void>, Close, (), (override));

  MOCK_METHOD(awaitable<ErrorOr<size_t>>,
              SendTo,
              (Endpoint endpoint, std::span<const char> datagram),
              (override));
};

}  // namespace

class AsioUdpTransportTest : public Test {
 public:
  virtual void SetUp() override;

  void OpenTransport(bool active);
  void ReceiveMessage();

  void ExpectTransportAccepted();

  boost::asio::system_executor executor_;
  std::shared_ptr<MockUdpSocket> socket = std::make_shared<MockUdpSocket>();
  UdpSocketContext::OpenHandler open_handler;
  UdpSocketContext::MessageHandler message_handler;

  UdpSocketFactory udp_socket_factory = [&](UdpSocketContext&& context) {
    open_handler = std::move(context.open_handler_);
    message_handler = std::move(context.message_handler_);
    return socket;
  };

  MockTransportHandlers transport_handlers_;
  std::unique_ptr<Transport> transport_;

  MockTransportHandlers accepted_transport_handlers_;
  std::unique_ptr<Transport> accepted_transport_;
};

void AsioUdpTransportTest::SetUp() {}

void AsioUdpTransportTest::OpenTransport(bool active) {
  transport_ = std::make_unique<AsioUdpTransport>(
      boost::asio::system_executor{}, NullLogger::GetInstance(),
      udp_socket_factory,
      /*host=*/std::string{},
      /*service=*/std::string{},
      /*active=*/active);

  EXPECT_CALL(*socket, Open());

  boost::asio::co_spawn(executor_,
                        transport_->Open(transport_handlers_.AsHandlers()),
                        boost::asio::detached);

  EXPECT_FALSE(transport_->IsActive());
  EXPECT_FALSE(transport_->IsConnected());

  const UdpSocket::Endpoint endpoint;
  EXPECT_CALL(transport_handlers_.on_open, Call());
  open_handler(endpoint);
  EXPECT_TRUE(transport_->IsConnected());
}

void AsioUdpTransportTest::ReceiveMessage() {
  const UdpSocket::Endpoint peer_endpoint;
  UdpSocket::Datagram datagram;
  message_handler(peer_endpoint, std::move(datagram));
}

void AsioUdpTransportTest::ExpectTransportAccepted() {
  EXPECT_CALL(transport_handlers_.on_accept, Call(_))
      .WillOnce(Invoke([&](std::unique_ptr<Transport> t) {
        accepted_transport_ = std::move(t);

        boost::asio::co_spawn(executor_,
                              accepted_transport_->Open(
                                  accepted_transport_handlers_.AsHandlers()),
                              boost::asio::detached);

        return net::OK;
      }));
}

TEST_F(AsioUdpTransportTest, UdpServer_AcceptedTransportIgnored) {
  OpenTransport(false);

  EXPECT_CALL(transport_handlers_.on_accept, Call(_))
      .WillOnce(Invoke([&](std::unique_ptr<Transport> t) {
        // Ignore accepted transport.
        return net::OK;
      }));

  ReceiveMessage();

  EXPECT_CALL(*socket, Close());
}

TEST_F(AsioUdpTransportTest, UdpServer_AcceptedTransportDestroyed) {
  OpenTransport(false);
  ExpectTransportAccepted();

  //EXPECT_CALL(accepted_transport_handlers_.on_message, Call(_));
  ReceiveMessage();

  ASSERT_TRUE(accepted_transport_);
  EXPECT_TRUE(accepted_transport_->IsConnected());

  accepted_transport_.reset();

  EXPECT_CALL(*socket, Close());
}

TEST_F(AsioUdpTransportTest, UdpServer_AcceptedTransportClosed) {
  OpenTransport(false);
  ExpectTransportAccepted();

  //EXPECT_CALL(accepted_transport_handlers_.on_message, Call(_));
  ReceiveMessage();

  ASSERT_TRUE(accepted_transport_);
  EXPECT_TRUE(accepted_transport_->IsConnected());

  accepted_transport_->Close();
  EXPECT_FALSE(accepted_transport_->IsConnected());

  EXPECT_CALL(*socket, Close());
}

TEST_F(AsioUdpTransportTest,
       UdpServer_AcceptedTransportDestroyedFromMessageHandler) {
  OpenTransport(false);
  ExpectTransportAccepted();

  /*EXPECT_CALL(accepted_transport_handlers_.on_message, Call(_))
      .WillOnce(Invoke([&] { accepted_transport_.reset(); }));*/
  ReceiveMessage();

  EXPECT_CALL(*socket, Close());
}

TEST_F(AsioUdpTransportTest,
       UdpServer_AcceptedTransportClosedFromMessageHandler) {
  OpenTransport(false);
  ExpectTransportAccepted();

  /*EXPECT_CALL(accepted_transport_handlers_.on_message, Call(_))
      .WillOnce(Invoke([&] { accepted_transport_->Close(); }));*/

  ReceiveMessage();

  EXPECT_CALL(*socket, Close());
}

}  // namespace net
