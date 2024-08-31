#include "transport/message_reader_transport.h"

#include "transport/test/coroutine_util.h"
#include "transport/test/immediate_executor.h"
#include "transport/test/test_message_reader.h"
#include "transport/transport_mock.h"

#include <array>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <cstring>
#include <gmock/gmock.h>

using namespace std::chrono_literals;
using namespace testing;

namespace transport {

class MessageReaderTransportTest : public Test {
 public:
  MessageReaderTransportTest();

  void CreateMessageReaderTransport(bool message_oriented);

  void ExpectChildReadSome(const std::vector<std::vector<char>>& fragments);
  void ExpectChildReadMessage(const std::vector<char>& message);
  [[nodiscard]] awaitable<expected<std::vector<char>>> ReadMessage();

  boost::asio::io_context io_context_;
  executor executor_ = io_context_.get_executor();

  TransportMock* child_transport_ = nullptr;

  std::unique_ptr<MessageReaderTransport> message_reader_transport_;
};

MessageReaderTransportTest::MessageReaderTransportTest() {}

void MessageReaderTransportTest::CreateMessageReaderTransport(
    bool message_oriented) {
  auto child_transport = std::make_unique<NiceMock<TransportMock>>();

  ON_CALL(*child_transport, message_oriented())
      .WillByDefault(Return(message_oriented));

  ON_CALL(*child_transport, open()).WillByDefault(CoReturn(OK));
  ON_CALL(*child_transport, close()).WillByDefault(CoReturn(OK));
  ON_CALL(*child_transport, connected()).WillByDefault(Return(true));
  ON_CALL(*child_transport, get_executor()).WillByDefault(Return(executor_));

  child_transport_ = child_transport.get();

  auto message_reader = std::make_unique<TestMessageReader>();

  message_reader_transport_ = std::make_unique<MessageReaderTransport>(
      any_transport{std::move(child_transport)}, std::move(message_reader));
}

void MessageReaderTransportTest::ExpectChildReadSome(
    const std::vector<std::vector<char>>& fragments) {
  // GMock clears mutable captured variables, so we need to store the fragments
  // in a shared pointer.
  auto shared_fragments =
      std::make_shared<std::vector<std::vector<char>>>(fragments);

  EXPECT_CALL(*child_transport_, read(/*buffer=*/_))
      .Times(fragments.size())
      .WillRepeatedly(Invoke([shared_fragments](std::span<char> data)
                                 -> awaitable<expected<size_t>> {
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

void MessageReaderTransportTest::ExpectChildReadMessage(
    const std::vector<char>& message) {
  EXPECT_CALL(*child_transport_, read(/*buffer=*/_))
      .WillOnce(Invoke(
          [message](std::span<char> data) -> awaitable<expected<size_t>> {
            if (data.size() < message.size()) {
              co_return ERR_FAILED;
            }
            std::ranges::copy(message, data.begin());
            co_return message.size();
          }));
}

awaitable<expected<std::vector<char>>>
MessageReaderTransportTest::ReadMessage() {
  std::array<char, 100> buffer;
  if (auto bytes_read = co_await message_reader_transport_->read(buffer);
      bytes_read.ok()) {
    co_return std::vector<char>{buffer.begin(), buffer.begin() + *bytes_read};
  } else {
    co_return bytes_read.error();
  }
}

TEST_F(MessageReaderTransportTest, SplitCompositeChildMessage) {
  CoTest([&]() -> awaitable<void> {
    CreateMessageReaderTransport(/*message_oriented=*/true);
    co_await message_reader_transport_->open();

    ExpectChildReadMessage({1, 0, 2, 0, 0, 3, 0, 0, 0});

    const auto message1 = std::vector<char>{1, 0};
    const auto message2 = std::vector<char>{2, 0, 0};
    const auto message3 = std::vector<char>{3, 0, 0, 0};

    EXPECT_EQ(co_await ReadMessage(), message1);
    EXPECT_EQ(co_await ReadMessage(), message2);
    EXPECT_EQ(co_await ReadMessage(), message3);
  });
}

TEST_F(MessageReaderTransportTest, CompositeMessage_LongerSize) {
  CoTest([&]() -> awaitable<void> {
    CreateMessageReaderTransport(/*message_oriented=*/true);
    co_await message_reader_transport_->open();

    ExpectChildReadMessage({5, 0, 0, 0});

    EXPECT_EQ(co_await ReadMessage(), ERR_FAILED);
  });
}

TEST_F(MessageReaderTransportTest, CompositeMessage_DestroyInTheMiddle) {
  CoTest([&]() -> awaitable<void> {
    CreateMessageReaderTransport(/*message_oriented=*/true);
    co_await message_reader_transport_->open();

    ExpectChildReadMessage({1, 0, 2, 0, 0, 3, 0, 0, 0});

    const auto message1 = std::vector<char>{1, 0};
    const auto message2 = std::vector<char>{2, 0, 0};

    EXPECT_EQ(co_await ReadMessage(), message1);
    EXPECT_EQ(co_await ReadMessage(), message2);
  });
}

TEST_F(MessageReaderTransportTest, CompositeMessage_CloseInTheMiddle) {
  CoTest([&]() -> awaitable<void> {
    CreateMessageReaderTransport(/*message_oriented=*/true);
    co_await message_reader_transport_->open();

    ExpectChildReadMessage({1, 0, 2, 0, 0, 3, 0, 0, 0});

    const auto message1 = std::vector<char>{1, 0};
    const auto message2 = std::vector<char>{2, 0, 0};

    EXPECT_EQ(co_await ReadMessage(), message1);
    EXPECT_EQ(co_await ReadMessage(), message2);

    EXPECT_CALL(*child_transport_, close());
    EXPECT_EQ(co_await message_reader_transport_->close(), OK);

    EXPECT_EQ(co_await ReadMessage(), ERR_CONNECTION_CLOSED);
  });
}

TEST_F(
    MessageReaderTransportTest,
    DISABLED_OnData_StreamChildTransport_PartialMessage_DontTriggerOnMessage) {
  CoTest([&]() -> awaitable<void> {
    CreateMessageReaderTransport(/*message_oriented=*/false);
    co_await message_reader_transport_->open();

    ExpectChildReadSome({{10}, {1, 2, 3}, {4}});

    auto _ = co_await ReadMessage();
  });
}

TEST_F(MessageReaderTransportTest,
       DISABLED_OnData_StreamChildTransport_FullMessage_TriggersOnMessage) {
  CoTest([&]() -> awaitable<void> {
    CreateMessageReaderTransport(/*message_oriented=*/false);

    ExpectChildReadSome(
        /*fragments=*/{{10}, {1, 2, 3}, {4, 5, 6, 7, 8, 9, 10}});

    const auto full_message =
        std::vector<char>{10, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10};

    EXPECT_EQ(co_await ReadMessage(), full_message);
  });
}

TEST_F(MessageReaderTransportTest, ImmediatelyDestroysChildTransportOnDestroy) {
  CreateMessageReaderTransport(/*message_oriented=*/false);

  // Start a read operation that may reference the child transport.
  boost::asio::co_spawn(
      executor_, [&]() -> awaitable<void> { auto _ = co_await ReadMessage(); },
      boost::asio::detached);

  EXPECT_CALL(*child_transport_, destroy());

  message_reader_transport_.reset();

  Mock::VerifyAndClearExpectations(child_transport_);
}

}  // namespace transport
