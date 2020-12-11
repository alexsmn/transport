#include "net/message_transport.h"

#include "net/message_reader.h"
#include "net/transport_delegate_mock.h"
#include "net/transport_mock.h"

#include <gmock/gmock.h>

namespace net {

using namespace testing;

class TestMessageReader : public MessageReaderImpl<1024> {
 public:
  virtual MessageReader* Clone() override {
    assert(false);
    return nullptr;
  }

 protected:
  virtual bool GetBytesExpected(const void* buf,
                                size_t len,
                                size_t& expected) const override {
    if (len < 1) {
      expected = 1;
      return true;
    }

    const char* bytes = static_cast<const char*>(buf);
    expected = 1 + static_cast<size_t>(bytes[0]);
    return true;
  }
};

class MessageTransportTest : public Test {
 public:
  virtual void SetUp() override;

  TransportDelegateMock message_transport_delegate_;

  Transport::Delegate* child_transport_delegate_ = nullptr;
  TransportMock* child_transport_ptr_ = nullptr;

  std::unique_ptr<MessageTransport> message_transport_;
};

void MessageTransportTest::SetUp() {
  auto child_transport = std::make_unique<TransportMock>();
  child_transport_ptr_ = child_transport.get();

  EXPECT_CALL(*child_transport_ptr_, IsMessageOriented())
      .Times(AnyNumber())
      .WillRepeatedly(Return(true));

  EXPECT_CALL(*child_transport_ptr_, Open(_))
      .WillOnce(Invoke([&](Transport::Delegate& delegate) {
        child_transport_delegate_ = &delegate;
        return net::OK;
      }));

  auto message_reader = std::make_unique<TestMessageReader>();
  message_transport_ = std::make_unique<MessageTransport>(
      std::move(child_transport), std::move(message_reader));

  ASSERT_EQ(net::OK, message_transport_->Open(message_transport_delegate_));
  ASSERT_TRUE(child_transport_delegate_);

  EXPECT_CALL(*child_transport_ptr_, IsConnected())
      .Times(AnyNumber())
      .WillRepeatedly(Return(true));
}

MATCHER_P2(HasBytes, bytes, size, "") {
  return std::memcmp(arg, bytes, size) == 0;
}

TEST_F(MessageTransportTest, CompositeMessage) {
  const char message1[] = {1, 0};
  const char message2[] = {2, 0, 0};
  const char message3[] = {3, 0, 0, 0};

  EXPECT_CALL(
      message_transport_delegate_,
      OnTransportMessageReceived(HasBytes(message1, std::size(message1)),
                                 std::size(message1)));
  EXPECT_CALL(
      message_transport_delegate_,
      OnTransportMessageReceived(HasBytes(message2, std::size(message2)),
                                 std::size(message2)));
  EXPECT_CALL(
      message_transport_delegate_,
      OnTransportMessageReceived(HasBytes(message3, std::size(message3)),
                                 std::size(message3)));

  const char datagram[] = {1, 0, 2, 0, 0, 3, 0, 0, 0};
  child_transport_delegate_->OnTransportMessageReceived(datagram,
                                                        std::size(datagram));

  EXPECT_CALL(*child_transport_ptr_, Close());
}

TEST_F(MessageTransportTest, CompositeMessage_LongerSize) {
  EXPECT_CALL(message_transport_delegate_, OnTransportMessageReceived(_, _))
      .Times(0);

  EXPECT_CALL(*child_transport_ptr_, Close());
  EXPECT_CALL(message_transport_delegate_, OnTransportClosed(ERR_FAILED));

  const char datagram[] = {5, 0, 0, 0};
  child_transport_delegate_->OnTransportMessageReceived(datagram,
                                                        std::size(datagram));

  EXPECT_CALL(*child_transport_ptr_, IsConnected())
      .Times(AnyNumber())
      .WillRepeatedly(Return(false));
}

TEST_F(MessageTransportTest, CompositeMessage_DestroyInTheMiddle) {
  const char message1[] = {1, 0};
  const char message2[] = {2, 0, 0};

  EXPECT_CALL(
      message_transport_delegate_,
      OnTransportMessageReceived(HasBytes(message1, std::size(message1)),
                                 std::size(message1)));
  EXPECT_CALL(message_transport_delegate_,
              OnTransportMessageReceived(
                  HasBytes(message2, std::size(message2)), std::size(message2)))
      .WillOnce(Invoke([&] { message_transport_.reset(); }));
  EXPECT_CALL(*child_transport_ptr_, Close());

  const char datagram[] = {1, 0, 2, 0, 0, 3, 0, 0, 0};
  child_transport_delegate_->OnTransportMessageReceived(datagram,
                                                        std::size(datagram));
}

TEST_F(MessageTransportTest, CompositeMessage_CloseInTheMiddle) {
  const char message1[] = {1, 0};
  const char message2[] = {2, 0, 0};

  EXPECT_CALL(
      message_transport_delegate_,
      OnTransportMessageReceived(HasBytes(message1, std::size(message1)),
                                 std::size(message1)));
  EXPECT_CALL(message_transport_delegate_,
              OnTransportMessageReceived(
                  HasBytes(message2, std::size(message2)), std::size(message2)))
      .WillOnce(Invoke([&] { message_transport_->Close(); }));
  EXPECT_CALL(*child_transport_ptr_, Close());

  const char datagram[] = {1, 0, 2, 0, 0, 3, 0, 0, 0};
  child_transport_delegate_->OnTransportMessageReceived(datagram,
                                                        std::size(datagram));

  EXPECT_CALL(*child_transport_ptr_, IsConnected())
      .Times(AnyNumber())
      .WillRepeatedly(Return(false));
}

}  // namespace net