#include "transport/any_transport.h"

#include "transport/transport_mock.h"

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/io_context.hpp>
#include <gmock/gmock.h>

#include <memory>
#include <optional>
#include <utility>

using namespace testing;

namespace transport {
namespace {

const char kData[] = {1, 2, 3};

// Runs `aw` to completion on a private io_context and returns its result.
template <class T>
expected<T> RunAwaitable(awaitable<expected<T>> aw) {
  boost::asio::io_context io_context;
  std::optional<expected<T>> result;

  boost::asio::co_spawn(
      io_context, [&]() -> awaitable<void> { result = co_await std::move(aw); },
      boost::asio::detached);

  io_context.run();

  EXPECT_TRUE(result.has_value());
  return std::move(*result);
}

// Regression tests for the 2026-08-01 production SIGSEGV on the GCP demo: the
// iec104 tier faulted inside `any_transport::write`'s coroutine body while
// reading `transport_` out of an already-freed `any_transport`.
//
// `read`/`write` used to be coroutines, so their `!transport_` guard ran on
// first *resume*, not at call time. A wrapper destroyed in that window left the
// guard reading freed memory: it saw non-null garbage, passed, and the next
// dereference faulted. Reading the member eagerly — while the caller is still
// inside the call — is what makes the guard a guard.
//
// These reproduce the original crash deterministically under guard malloc,
// which unmaps the freed page (a plain run passes — the stale bytes read back
// benignly, which is exactly why this survived so long in production):
//   DYLD_INSERT_LIBRARIES=/usr/lib/libgmalloc.dylib ./transport_unittests
TEST(AnyTransportTest, WriteAfterTheWrapperIsDestroyedDoesNotTouchFreedMemory) {
  auto transport = std::make_unique<any_transport>(
      std::make_unique<NiceMock<TransportMock>>());

  // The awaitable is obtained while the wrapper is alive, as a caller holding
  // an `any_transport*` does; the wrapper dies before it is awaited.
  auto pending = transport->write(kData);
  transport.reset();

  const expected<size_t> result = RunAwaitable(std::move(pending));

  // The operation completes against the retained transport instead of reading
  // freed memory. ERR_ABORTED is TransportMock's default write result.
  ASSERT_FALSE(result.ok());
  EXPECT_EQ(result.error(), ERR_ABORTED);
}

TEST(AnyTransportTest, ReadAfterTheWrapperIsDestroyedDoesNotTouchFreedMemory) {
  auto transport = std::make_unique<any_transport>(
      std::make_unique<NiceMock<TransportMock>>());

  char buffer[8] = {};
  auto pending = transport->read(buffer);
  transport.reset();

  const expected<size_t> result = RunAwaitable(std::move(pending));

  ASSERT_FALSE(result.ok());
  EXPECT_EQ(result.error(), ERR_ABORTED);
}

// Destroying the wrapper must still destroy the underlying transport promptly:
// that is how this codebase cancels operations already in flight. An earlier
// attempt at the fix above kept the transport alive from the pending operation,
// which deadlocked teardown — the read only completes once the transport goes
// away, and the transport only went away once the read completed. Two
// `ServerConnectionClosesGracefully...` module tests caught it.
TEST(AnyTransportTest, DestroyingTheWrapperDestroysTheTransportImmediately) {
  auto mock = std::make_unique<NiceMock<TransportMock>>();
  auto& mock_ref = *mock;

  bool destroyed = false;
  EXPECT_CALL(mock_ref, destroy()).WillOnce(Assign(&destroyed, true));

  auto transport = std::make_unique<any_transport>(std::move(mock));

  auto pending = transport->write(kData);
  transport.reset();

  EXPECT_TRUE(destroyed)
      << "a pending operation must not keep the transport alive";
}

// A transport that is already empty when the call is made still reports the
// error rather than dispatching — the guard has to survive becoming eager.
TEST(AnyTransportTest, WriteOnAnEmptyTransportReportsInvalidHandle) {
  any_transport transport;

  const expected<size_t> result = RunAwaitable(transport.write(kData));

  ASSERT_FALSE(result.ok());
  EXPECT_EQ(result.error(), ERR_INVALID_HANDLE);
}

// A moved-from wrapper is the teardown state `close()` documents, and must keep
// reporting an invalid handle rather than dispatching to the moved-out
// transport.
TEST(AnyTransportTest, WriteOnAMovedFromTransportReportsInvalidHandle) {
  any_transport transport{std::make_unique<NiceMock<TransportMock>>()};
  any_transport moved_away = std::move(transport);

  const expected<size_t> result = RunAwaitable(transport.write(kData));

  ASSERT_FALSE(result.ok());
  EXPECT_EQ(result.error(), ERR_INVALID_HANDLE);
}

// A live transport must still reach the underlying implementation — the eager
// read must not turn every call into ERR_INVALID_HANDLE.
TEST(AnyTransportTest, WriteReachesTheUnderlyingTransport) {
  auto mock = std::make_unique<NiceMock<TransportMock>>();
  EXPECT_CALL(*mock, write(_))
      .WillOnce(Return(ByMove(CoValue(expected<size_t>{sizeof(kData)}))));

  any_transport transport{std::move(mock)};

  const expected<size_t> result = RunAwaitable(transport.write(kData));

  ASSERT_TRUE(result.ok());
  EXPECT_EQ(result.value(), sizeof(kData));
}

}  // namespace
}  // namespace transport
