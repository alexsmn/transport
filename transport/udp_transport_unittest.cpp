#include "transport/udp_transport.h"

#include "transport/any_transport.h"
#include "transport/log.h"
#include "transport/test/coroutine_util.h"
#include "transport/udp_socket.h"

#include <gmock/gmock.h>
#include <optional>

namespace transport {

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

  MOCK_METHOD(awaitable<Error>, Open, (), (override));
  MOCK_METHOD(awaitable<void>, Close, (), (override));

  MOCK_METHOD(awaitable<ErrorOr<size_t>>,
              SendTo,
              (Endpoint endpoint, std::span<const char> datagram),
              (override));
};

}  // namespace

class UdpTransportTest : public Test {
 public:
  virtual void SetUp() override;
  virtual void TearDown() override;

  [[nodiscard]] any_transport OpenTransport(bool active);
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

void UdpTransportTest::SetUp() {}

void UdpTransportTest::TearDown() {}

any_transport UdpTransportTest::OpenTransport(bool active) {
  auto transport = active ? any_transport{std::make_unique<ActiveUdpTransport>(
                                executor_, log_source{}, udp_socket_factory,
                                /*host=*/std::string{},
                                /*service=*/std::string{})}
                          : any_transport{std::make_unique<PassiveUdpTransport>(
                                executor_, log_source{}, udp_socket_factory,
                                /*host=*/std::string{},
                                /*service=*/std::string{})};

  EXPECT_CALL(*socket, Open());

  boost::asio::co_spawn(transport.get_executor(), transport.open(),
                        boost::asio::detached);

  EXPECT_FALSE(transport.active());
  EXPECT_FALSE(transport.connected());

  const UdpSocket::Endpoint endpoint;
  open_handler(endpoint);

  EXPECT_TRUE(transport.connected());

  return transport;
}

void UdpTransportTest::ReceiveMessage() {
  const UdpSocket::Endpoint peer_endpoint;
  UdpSocket::Datagram datagram;
  message_handler(peer_endpoint, std::move(datagram));
}

TEST_F(UdpTransportTest, UdpServer_AcceptedTransportImmediatelyDestroyed) {
  auto transport = OpenTransport(/*active=*/false);
  ReceiveMessage();

  CoTest([&]() -> awaitable<void> {
    auto accepted_transport = co_await transport.accept();
    EXPECT_TRUE(accepted_transport.ok());
  });

  EXPECT_CALL(*socket, Close());
}

TEST_F(UdpTransportTest, UdpServer_AcceptedTransportReceiveMessage) {
  auto transport = OpenTransport(/*active=*/false);
  ReceiveMessage();

  CoTest([&]() -> awaitable<void> {
    auto accepted_transport = co_await transport.accept();
    EXPECT_TRUE(accepted_transport.ok());

    std::array<char, 1024> buffer;
    auto received_message = co_await accepted_transport->read(buffer);
    // TODO: Compare the message with the expected one.
    EXPECT_TRUE(received_message.ok());
  });

  EXPECT_CALL(*socket, Close());
}

TEST_F(UdpTransportTest, UdpServer_AcceptedTransportClosed) {
  auto transport = OpenTransport(/*active=*/false);
  ReceiveMessage();

  CoTest([&]() -> awaitable<void> {
    auto accepted_transport = co_await transport.accept();
    EXPECT_TRUE(accepted_transport.ok());
    EXPECT_EQ(co_await accepted_transport->close(), OK);
    EXPECT_FALSE(accepted_transport->connected());
  });

  EXPECT_CALL(*socket, Close());
}

#if 0
TEST_F(UdpTransportTest,
       UdpServer_AcceptedTransportDestroyedFromMessageHandler) {
  OpenTransport(false);
  ExpectTransportAccepted();

  /*EXPECT_CALL(accepted_transport_handlers_.on_message, Call(_))
      .WillOnce(Invoke([&] { accepted_transport_.reset(); }));*/
  ReceiveMessage();

  EXPECT_CALL(*socket, Close());
}

TEST_F(UdpTransportTest,
       UdpServer_AcceptedTransportClosedFromMessageHandler) {
  OpenTransport(false);
  ExpectTransportAccepted();

  /*EXPECT_CALL(accepted_transport_handlers_.on_message, Call(_))
      .WillOnce(Invoke([&] { accepted_transport_->Close(); }));*/

  ReceiveMessage();

  EXPECT_CALL(*socket, Close());
}
#endif

}  // namespace transport
