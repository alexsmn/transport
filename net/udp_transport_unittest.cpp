#include "net/udp_transport.h"

#include "net/logger.h"
#include "net/test/coroutine_util.h"
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
  virtual void TearDown() override;

  [[nodiscard]] std::unique_ptr<Transport> OpenTransport(bool active);
  void ReceiveMessage();

  Executor executor_ = boost::asio::system_executor{};
  std::shared_ptr<MockUdpSocket> socket = std::make_shared<MockUdpSocket>();
  UdpSocketContext::OpenHandler open_handler;
  UdpSocketContext::MessageHandler message_handler;

  UdpSocketFactory udp_socket_factory = [&](UdpSocketContext&& context) {
    open_handler = std::move(context.open_handler_);
    message_handler = std::move(context.message_handler_);
    return socket;
  };
};

void AsioUdpTransportTest::SetUp() {}

void AsioUdpTransportTest::TearDown() {}

std::unique_ptr<Transport> AsioUdpTransportTest::OpenTransport(bool active) {
  auto transport = std::make_unique<AsioUdpTransport>(
      executor_, NullLogger::GetInstance(), udp_socket_factory,
      /*host=*/std::string{},
      /*service=*/std::string{},
      /*active=*/active);

  EXPECT_CALL(*socket, Open());

  boost::asio::co_spawn(transport->GetExecutor(), transport->Open(),
                        boost::asio::detached);

  EXPECT_FALSE(transport->IsActive());
  EXPECT_FALSE(transport->IsConnected());

  const UdpSocket::Endpoint endpoint;
  open_handler(endpoint);

  EXPECT_TRUE(transport->IsConnected());

  return transport;
}

void AsioUdpTransportTest::ReceiveMessage() {
  const UdpSocket::Endpoint peer_endpoint;
  UdpSocket::Datagram datagram;
  message_handler(peer_endpoint, std::move(datagram));
}

TEST_F(AsioUdpTransportTest, UdpServer_AcceptedTransportImmediatelyDestroyed) {
  auto transport = OpenTransport(/*active=*/false);
  ReceiveMessage();

  CoTest([&]() -> awaitable<void> {
    auto accepted_transport = co_await transport->Accept();
    EXPECT_TRUE(accepted_transport.ok());
  });

  EXPECT_CALL(*socket, Close());
}

TEST_F(AsioUdpTransportTest, UdpServer_AcceptedTransportReceiveMessage) {
  auto transport = OpenTransport(/*active=*/false);
  ReceiveMessage();

  CoTest([&]() -> awaitable<void> {
    auto accepted_transport = co_await transport->Accept();
    EXPECT_TRUE(accepted_transport.ok());

    std::array<char, 1024> buffer;
    auto received_message = co_await (*accepted_transport)->Read(buffer);
    // TODO: Compare the message with the expected one.
    EXPECT_TRUE(received_message.ok());
  });

  EXPECT_CALL(*socket, Close());
}

TEST_F(AsioUdpTransportTest, UdpServer_AcceptedTransportClosed) {
  auto transport = OpenTransport(/*active=*/false);
  ReceiveMessage();

  CoTest([&]() -> awaitable<void> {
    auto accepted_transport = co_await transport->Accept();
    EXPECT_TRUE(accepted_transport.ok());

    (*accepted_transport)->Close();
    EXPECT_FALSE((*accepted_transport)->IsConnected());
  });

  EXPECT_CALL(*socket, Close());
}

#if 0
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
#endif

}  // namespace net
