#include "net/message_transport.h"

#include "net/message_reader.h"
#include "net/transport_delegate_mock.h"

#include <gmock/gmock.h>

namespace net {

using namespace testing;

class TestTransport : public Transport {
 public:
  // Transport
  virtual Error Open(Delegate& delegate) override {
    delegate_ = &delegate;
    return net::OK;
  }
  virtual void Close() override {}
  virtual int Read(void* data, size_t len) override { return net::ERR_FAILED; }
  virtual int Write(const void* data, size_t len) override {
    return net::ERR_FAILED;
  }
  virtual std::string GetName() const override { return "TestTransport"; }
  virtual bool IsMessageOriented() const override { return true; }
  virtual bool IsConnected() const override { return true; }
  virtual bool IsActive() const override { return true; }

  Delegate* delegate_ = nullptr;
};

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

MATCHER_P2(HasBytes, bytes, size, "") {
  return std::memcmp(arg, bytes, size) == 0;
}

TEST(MessageTransport, Test) {
  auto child_transport = std::make_unique<TestTransport>();
  auto& child_transport_ref = *child_transport;
  auto message_reader = std::make_unique<TestMessageReader>();

  MessageTransport message_transport(std::move(child_transport),
                                     std::move(message_reader));

  TransportDelegateMock delegate;
  ASSERT_EQ(net::OK, message_transport.Open(delegate));

  ASSERT_TRUE(child_transport_ref.delegate_);

  const char message1[] = {1, 0};
  const char message2[] = {2, 0, 0};
  const char message3[] = {3, 0, 0, 0};
  EXPECT_CALL(delegate, OnTransportMessageReceived(
                            HasBytes(message1, std::size(message1)),
                            std::size(message1)));
  EXPECT_CALL(delegate, OnTransportMessageReceived(
                            HasBytes(message2, std::size(message2)),
                            std::size(message2)));
  EXPECT_CALL(delegate, OnTransportMessageReceived(
                            HasBytes(message3, std::size(message3)),
                            std::size(message3)));

  const char datagram[] = {1, 0, 2, 0, 0, 3, 0, 0, 0};
  child_transport_ref.delegate_->OnTransportMessageReceived(
      datagram, std::size(datagram));
}

}  // namespace net