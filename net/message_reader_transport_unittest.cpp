#include "net/message_reader_transport.h"

#include "net/test/immediate_executor.h"
#include "net/test/test_message_reader.h"
#include "net/transport_delegate_mock.h"
#include "net/transport_mock.h"

#include <array>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <cstring>
#include <gmock/gmock.h>

using namespace std::chrono_literals;
using namespace testing;

namespace net {

class MessageTransportTest : public Test {
 public:
  MessageTransportTest();

  void InitChildTransport(bool message_oriented);

  void ExpectChildReadSome(const std::vector<std::vector<char>>& fragments);
  void ExpectChildReadMessage(const std::vector<char>& message);
  [[nodiscard]] awaitable<ErrorOr<std::vector<char>>> ReadMessage();

  Executor executor_ = boost::asio::system_executor{};

  StrictMock<MockTransportHandlers> message_transport_handlers_;

  Transport::Handlers child_handlers_;

  TransportMock* child_transport_ = nullptr;

  std::unique_ptr<MessageReaderTransport> message_transport_;
};

MessageTransportTest::MessageTransportTest() {}

void MessageTransportTest::InitChildTransport(bool message_oriented) {
  auto child_transport = std::make_unique<StrictMock<TransportMock>>();

  EXPECT_CALL(*child_transport, IsMessageOriented())
      .Times(AnyNumber())
      .WillRepeatedly(Return(message_oriented));

  EXPECT_CALL(*child_transport, Open(/*handlers=*/_))
      .WillOnce(DoAll(SaveArg<0>(&child_handlers_), CoReturn(OK)));

  EXPECT_CALL(*child_transport, IsConnected())
      .Times(AnyNumber())
      .WillRepeatedly(Return(true));

  EXPECT_CALL(*child_transport, Close());
  EXPECT_CALL(*child_transport, Destroy());

  child_transport_ = child_transport.get();

  auto message_reader = std::make_unique<TestMessageReader>();

  message_transport_ = std::make_unique<MessageReaderTransport>(
      executor_, std::move(child_transport), std::move(message_reader),
      NullLogger::GetInstance());

  boost::asio::co_spawn(
      executor_,
      message_transport_->Open(message_transport_handlers_.AsHandlers()),
      boost::asio::detached);
}

void MessageTransportTest::ExpectChildReadSome(
    const std::vector<std::vector<char>>& fragments) {
  // GMock clears mutable captured variables, so we need to store the fragments
  // in a shared pointer.
  auto shared_fragments =
      std::make_shared<std::vector<std::vector<char>>>(fragments);

  EXPECT_CALL(*child_transport_, Read(/*buffer=*/_))
      .Times(fragments.size())
      .WillRepeatedly(Invoke([shared_fragments](std::span<char> data)
                                 -> awaitable<ErrorOr<size_t>> {
        if (shared_fragments->empty()) {
          co_return ERR_FAILED;
        }
        auto& buffer = shared_fragments->front();
        if (data.size() < buffer.size()) {
          co_return ERR_FAILED;
        }
        std::ranges::copy(buffer, data.begin());
        auto bytes_read = buffer.size();
        shared_fragments->erase(shared_fragments->begin());
        co_return bytes_read;
      }));
}

void MessageTransportTest::ExpectChildReadMessage(
    const std::vector<char>& message) {
  EXPECT_CALL(*child_transport_, Read(/*buffer=*/_))
      .WillOnce(
          Invoke([message](std::span<char> data) -> awaitable<ErrorOr<size_t>> {
            if (data.size() < message.size()) {
              co_return ERR_FAILED;
            }
            std::ranges::copy(message, data.begin());
            co_return message.size();
          }));
}

awaitable<ErrorOr<std::vector<char>>> MessageTransportTest::ReadMessage() {
  std::array<char, 100> buffer;
  if (auto bytes_read = co_await message_transport_->Read(buffer);
      bytes_read.ok()) {
    co_return std::vector<char>{buffer.begin(), buffer.begin() + *bytes_read};
  } else {
    co_return bytes_read.error();
  }
}

TEST_F(MessageTransportTest, SplitCompositeChildMessage) {
  boost::asio::co_spawn(
      executor_,
      [&]() -> awaitable<void> {
        InitChildTransport(/*message_oriented=*/true);

        ExpectChildReadMessage({1, 0, 2, 0, 0, 3, 0, 0, 0});

        const auto message1 = std::vector<char>{1, 0};
        const auto message2 = std::vector<char>{2, 0, 0};
        const auto message3 = std::vector<char>{3, 0, 0, 0};

        EXPECT_EQ(co_await ReadMessage(), message1);
        EXPECT_EQ(co_await ReadMessage(), message2);
        EXPECT_EQ(co_await ReadMessage(), message3);
      },
      boost::asio::detached);
}

TEST_F(MessageTransportTest, CompositeMessage_LongerSize) {
  boost::asio::co_spawn(
      executor_,
      [&]() -> awaitable<void> {
        InitChildTransport(/*message_oriented=*/true);

        ExpectChildReadMessage({5, 0, 0, 0});

        EXPECT_EQ(co_await ReadMessage(), ERR_FAILED);
      },
      boost::asio::detached);
}

TEST_F(MessageTransportTest, CompositeMessage_DestroyInTheMiddle) {
  boost::asio::co_spawn(
      executor_,
      [&]() -> awaitable<void> {
        InitChildTransport(/*message_oriented=*/true);

        ExpectChildReadMessage({1, 0, 2, 0, 0, 3, 0, 0, 0});

        const auto message1 = std::vector<char>{1, 0};
        const auto message2 = std::vector<char>{2, 0, 0};

        EXPECT_EQ(co_await ReadMessage(), message1);
        EXPECT_EQ(co_await ReadMessage(), message2);
      },
      boost::asio::detached);
}

TEST_F(MessageTransportTest, CompositeMessage_CloseInTheMiddle) {
  boost::asio::co_spawn(
      executor_,
      [&]() -> awaitable<void> {
        InitChildTransport(/*message_oriented=*/true);

        ExpectChildReadMessage({1, 0, 2, 0, 0, 3, 0, 0, 0});

        const auto message1 = std::vector<char>{1, 0};
        const auto message2 = std::vector<char>{2, 0, 0};

        EXPECT_EQ(co_await ReadMessage(), message1);
        EXPECT_EQ(co_await ReadMessage(), message2);

        message_transport_->Close();

        EXPECT_EQ(co_await ReadMessage(), ERR_CONNECTION_CLOSED);
      },
      boost::asio::detached);
}

TEST_F(
    MessageTransportTest,
    DISABLED_OnData_StreamChildTransport_PartialMessage_DontTriggerOnMessage) {
  InitChildTransport(/*message_oriented=*/false);

  ExpectChildReadSome({{10}, {1, 2, 3}, {4}});

  // Check read message doesn't complete.
  bool message_read = false;

  boost::asio::co_spawn(
      executor_,
      [&]() -> awaitable<void> {
        auto _ = co_await ReadMessage();
        message_read = true;
      },
      boost::asio::detached);

  EXPECT_FALSE(message_read);
}

TEST_F(MessageTransportTest,
       DISABLED_OnData_StreamChildTransport_FullMessage_TriggersOnMessage) {
  boost::asio::co_spawn(
      executor_,
      [&]() -> awaitable<void> {
        InitChildTransport(/*message_oriented=*/false);

        ExpectChildReadSome({{10}, {1, 2, 3}, {4, 5, 6, 7, 8, 9, 10}});

        const auto full_message =
            std::vector<char>{10, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10};

        EXPECT_EQ(co_await ReadMessage(), full_message);
      },
      boost::asio::detached);
}

}  // namespace net
