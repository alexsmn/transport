#include "net/message_reader_transport.h"

#include "net/message_reader.h"
#include "net/test/immediate_executor.h"
#include "net/transport_delegate_mock.h"
#include "net/transport_mock.h"

#include <boost/asio/io_context.hpp>
#include <cstring>
#include <gmock/gmock.h>

using namespace std::chrono_literals;
using namespace testing;

namespace net {

class MessageTransportTest : public Test {
 public:
  void InitChildTransport(bool message_oriented);

  ImmediateExecutor executor_;

  StrictMock<MockTransportHandlers> message_transport_handlers_;

  Transport::Handlers child_handlers_;
  TransportMock* child_transport_ptr_ = nullptr;

  std::unique_ptr<MessageReaderTransport> message_transport_;
};

namespace {

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

auto MakeReadImpl(const std::vector<char>& buffer) {
  return [buffer](std::span<char> data) {
    if (data.size() < buffer.size()) {
      throw std::runtime_error{"The read buffer is too small"};
    }
    std::ranges::copy(buffer, data.begin());
    return buffer.size();
  };
}

MATCHER_P2(HasBytes, bytes, size, "") {
  return std::memcmp(arg, bytes, size) == 0;
}

}  // namespace

void MessageTransportTest::InitChildTransport(bool message_oriented) {
  auto child_transport = std::make_unique<TransportMock>();
  child_transport_ptr_ = child_transport.get();

  EXPECT_CALL(*child_transport_ptr_, IsMessageOriented())
      .Times(AnyNumber())
      .WillRepeatedly(Return(message_oriented));

  EXPECT_CALL(*child_transport_ptr_, Open(_))
      .WillOnce(
          DoAll(SaveArg<0>(&child_handlers_), Return(make_resolved_promise())));

  EXPECT_CALL(*child_transport_ptr_, IsConnected())
      .Times(AnyNumber())
      .WillRepeatedly(Return(true));

  auto message_reader = std::make_unique<TestMessageReader>();

  message_transport_ = std::make_unique<MessageReaderTransport>(
      boost::asio::system_executor{}, std::move(child_transport),
      std::move(message_reader), NullLogger::GetInstance());

  message_transport_->Open(message_transport_handlers_.AsHandlers());

  EXPECT_CALL(*child_transport_ptr_, Close());
}

TEST_F(MessageTransportTest, CompositeMessage) {
  InitChildTransport(/*message_oriented=*/true);

  const char message1[] = {1, 0};
  const char message2[] = {2, 0, 0};
  const char message3[] = {3, 0, 0, 0};

  EXPECT_CALL(message_transport_handlers_.on_message,
              Call(ElementsAreArray(message1)));

  EXPECT_CALL(message_transport_handlers_.on_message,
              Call(ElementsAreArray(message2)));

  EXPECT_CALL(message_transport_handlers_.on_message,
              Call(ElementsAreArray(message3)));

  const char datagram[] = {1, 0, 2, 0, 0, 3, 0, 0, 0};
  child_handlers_.on_message(datagram);
}

TEST_F(MessageTransportTest, CompositeMessage_LongerSize) {
  InitChildTransport(/*message_oriented=*/true);

  EXPECT_CALL(message_transport_handlers_.on_message, Call(_)).Times(0);

  EXPECT_CALL(message_transport_handlers_.on_close, Call(ERR_FAILED));

  const char datagram[] = {5, 0, 0, 0};
  child_handlers_.on_message(datagram);

  EXPECT_CALL(*child_transport_ptr_, IsConnected())
      .Times(AnyNumber())
      .WillRepeatedly(Return(false));
}

TEST_F(MessageTransportTest, CompositeMessage_DestroyInTheMiddle) {
  InitChildTransport(/*message_oriented=*/true);

  const char message1[] = {1, 0};
  const char message2[] = {2, 0, 0};

  EXPECT_CALL(message_transport_handlers_.on_message,
              Call(ElementsAreArray(message1)));

  EXPECT_CALL(message_transport_handlers_.on_message,
              Call(ElementsAreArray(message2)))
      .WillOnce(Invoke([&] { message_transport_.reset(); }));

  const char datagram[] = {1, 0, 2, 0, 0, 3, 0, 0, 0};
  child_handlers_.on_message(datagram);
}

TEST_F(MessageTransportTest, CompositeMessage_CloseInTheMiddle) {
  InitChildTransport(/*message_oriented=*/true);

  const char message1[] = {1, 0};
  const char message2[] = {2, 0, 0};
  const char message3[] = {3, 0, 0, 0};

  EXPECT_CALL(message_transport_handlers_.on_message,
              Call(ElementsAreArray(message1)));

  EXPECT_CALL(message_transport_handlers_.on_message,
              Call(ElementsAreArray(message2)))
      .WillOnce(Invoke([&] { message_transport_->Close(); }));

  // It's allowed for the following messages to be dropped or be received.
  EXPECT_CALL(message_transport_handlers_.on_message,
              Call(ElementsAreArray(message3)))
      .Times(0);

  const char datagram[] = {1, 0, 2, 0, 0, 3, 0, 0, 0};
  child_handlers_.on_message(datagram);

  EXPECT_CALL(*child_transport_ptr_, IsConnected())
      .Times(AnyNumber())
      .WillRepeatedly(Return(false));
}

TEST_F(MessageTransportTest,
       OnData_StreamChildTransport_PartialMessage_DontTriggerOnMessage) {
  InitChildTransport(/*message_oriented=*/false);

  InSequence s;

  EXPECT_CALL(message_transport_handlers_.on_message, Call(_)).Times(0);

  EXPECT_CALL(*child_transport_ptr_, Read(SizeIs(1)))
      .WillOnce(Invoke(MakeReadImpl({10})));

  EXPECT_CALL(*child_transport_ptr_, Read(SizeIs(10)))
      .WillOnce(Invoke(MakeReadImpl({1, 2, 3})));

  EXPECT_CALL(*child_transport_ptr_, Read(SizeIs(7)))
      .WillOnce(Invoke(MakeReadImpl({4})));

  EXPECT_CALL(*child_transport_ptr_, Read(SizeIs(6)))
      .WillOnce(Invoke(MakeReadImpl({})));

  child_handlers_.on_data();
}

TEST_F(MessageTransportTest,
       OnData_StreamChildTransport_FullMessage_TriggersOnMessage) {
  InitChildTransport(/*message_oriented=*/false);

  InSequence s;

  EXPECT_CALL(*child_transport_ptr_, Read(SizeIs(1)))
      .WillOnce(Invoke(MakeReadImpl({10})));

  EXPECT_CALL(*child_transport_ptr_, Read(SizeIs(10)))
      .WillOnce(Invoke(MakeReadImpl({1, 2, 3})));

  EXPECT_CALL(*child_transport_ptr_, Read(SizeIs(7)))
      .WillOnce(Invoke(MakeReadImpl({4, 5, 6, 7, 8, 9, 10})));

  EXPECT_CALL(message_transport_handlers_.on_message,
              Call(ElementsAre(10, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10)));

  // Starts reading next message.

  EXPECT_CALL(*child_transport_ptr_, Read(SizeIs(1)))
      .WillOnce(Invoke(MakeReadImpl({})));

  child_handlers_.on_data();
}

}  // namespace net
