#include "transport/deferred_transport.h"

#include "transport/transport_mock.h"

#include <boost/asio/system_executor.hpp>
#include <gmock/gmock.h>

using namespace testing;

namespace transport {

class DeferredTransportTest : public Test {
 public:
  virtual void SetUp() override;

 protected:
  std::unique_ptr<DeferredTransport> deferred_transport_;

  // The underlying transport is owned by the deferred transport.
  TransportMock* underlying_transport_ = nullptr;
};

void DeferredTransportTest::SetUp() {
  auto underlying_transport = std::make_unique<NiceMock<TransportMock>>();
  underlying_transport_ = underlying_transport.get();

  deferred_transport_ = std::make_unique<DeferredTransport>(
      any_transport{std::move(underlying_transport)});
}

TEST_F(DeferredTransportTest, Destroy_DestroysUnderlyingTransport) {
  EXPECT_CALL(*underlying_transport_, destroy());

  deferred_transport_.reset();
}

TEST_F(DeferredTransportTest, Destroy_FiresCloseHandler) {
  bool close_handler_called = false;
  error_code close_error = OK;

  deferred_transport_->set_additional_close_handler(
      [&](error_code error) {
        close_handler_called = true;
        close_error = error;
      });

  deferred_transport_.reset();

  EXPECT_TRUE(close_handler_called);
  EXPECT_EQ(close_error, ERR_ABORTED);
}

TEST_F(DeferredTransportTest, Destroy_NoCloseHandler_DoesNotCrash) {
  // No close handler set — destroy should not crash.
  deferred_transport_.reset();
}

TEST_F(DeferredTransportTest, Close_FiresCloseHandler_DestroyDoesNotFireAgain) {
  int close_handler_call_count = 0;

  deferred_transport_->set_additional_close_handler(
      [&](error_code) { ++close_handler_call_count; });

  ON_CALL(*underlying_transport_, close()).WillByDefault(CoReturn(OK));

  CoTest([&]() -> awaitable<void> {
    co_await deferred_transport_->close();
  });

  EXPECT_EQ(close_handler_call_count, 1);

  // Destroy should not fire the handler again since close() already consumed it.
  deferred_transport_.reset();

  EXPECT_EQ(close_handler_call_count, 1);
}

TEST_F(DeferredTransportTest, ReadReturnsZero_FiresCloseHandler_DestroyDoesNotFireAgain) {
  int close_handler_call_count = 0;

  deferred_transport_->set_additional_close_handler(
      [&](error_code) { ++close_handler_call_count; });

  ON_CALL(*underlying_transport_, read(_))
      .WillByDefault(CoReturn(expected<size_t>{size_t{0}}));

  CoTest([&]() -> awaitable<void> {
    char buf[16];
    auto result = co_await deferred_transport_->read(buf);
    EXPECT_TRUE(result.ok());
    EXPECT_EQ(*result, 0u);
  });

  EXPECT_EQ(close_handler_call_count, 1);

  // Destroy should not fire again — OnClosed already cleared the handler.
  deferred_transport_.reset();

  EXPECT_EQ(close_handler_call_count, 1);
}

}  // namespace transport