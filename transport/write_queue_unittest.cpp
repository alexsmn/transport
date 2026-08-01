#include "transport/write_queue.h"

#include "transport/any_transport.h"
#include "transport/transport_mock.h"

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/experimental/channel.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <gmock/gmock.h>

#include <memory>
#include <optional>
#include <utility>

using namespace testing;

namespace transport {
namespace {

using Gate =
    boost::asio::experimental::channel<void(boost::system::error_code)>;

// A write that does not finish until `gate` is released.
awaitable<expected<size_t>> GatedWrite(Gate& gate, size_t size) {
  co_await gate.async_receive(boost::asio::use_awaitable);
  co_return size;
}

// Regression test: a WriteQueue whose transport was moved out (teardown
// paths do this, e.g. closing a connection moves the transport into a close
// coroutine) must drop blind writes instead of co_spawning on the empty
// executor, which throws boost::asio::execution::bad_executor.
TEST(WriteQueueTest, BlindWriteAfterTransportMovedOutIsNoOp) {
  any_transport transport{std::make_unique<NiceMock<TransportMock>>()};
  WriteQueue write_queue{transport};

  any_transport moved_away = std::move(transport);

  const char kData[] = {1, 2, 3};
  write_queue.BlindWrite(kData);
}

// Regression test for the 2026-08-01 production SIGSEGV on the demo's iec104
// tier (see any_transport_unittest.cpp for the other half).
//
// `State` borrows a raw `any_transport*` from its owner, and a write queued
// behind another resumes long after that owner may be gone. The
// `cancelation.expired()` check guarding the dereference sat in exactly the
// right place but could never fire: the cancelation token lived *in* `State`,
// which every queued coroutine keeps alive by holding the state. Owning the
// token in the WriteQueue itself is what arms it.
//
// Run under guard malloc to catch a regression as a fault rather than as a
// benign read of freed memory:
//   DYLD_INSERT_LIBRARIES=/usr/lib/libgmalloc.dylib ./transport_unittests
TEST(WriteQueueTest, QueuedWriteAbortsWhenTheQueueDiesWhileItWaits) {
  boost::asio::io_context io_context;
  Gate gate{io_context.get_executor(), /*max_buffer_size=*/1};

  auto mock = std::make_unique<NiceMock<TransportMock>>();
  ON_CALL(*mock, get_executor())
      .WillByDefault(Return(io_context.get_executor()));
  ON_CALL(*mock, write(_))
      .WillByDefault(Invoke(
          [&gate](std::span<const char> data) -> awaitable<expected<size_t>> {
            return GatedWrite(gate, data.size());
          }));

  auto transport = std::make_unique<any_transport>(std::move(mock));
  auto write_queue = std::make_unique<WriteQueue>(*transport);

  const char kData[] = {1, 2, 3};
  std::optional<expected<size_t>> first;
  std::optional<expected<size_t>> second;

  boost::asio::co_spawn(
      io_context,
      [&]() -> awaitable<void> { first = co_await write_queue->Write(kData); },
      boost::asio::detached);
  boost::asio::co_spawn(
      io_context,
      [&]() -> awaitable<void> { second = co_await write_queue->Write(kData); },
      boost::asio::detached);

  // Let both reach their suspension points: the first inside the transport's
  // write, the second parked behind it.
  io_context.poll();
  ASSERT_FALSE(first.has_value());
  ASSERT_FALSE(second.has_value());

  // The owner goes away mid-flight, taking the transport the queue points at.
  write_queue.reset();
  transport.reset();

  gate.try_send(boost::system::error_code{});
  io_context.run();

  // The queued write must abort rather than dereference the freed transport.
  ASSERT_TRUE(second.has_value());
  EXPECT_FALSE(second->ok());
  EXPECT_EQ(second->error(), ERR_ABORTED);
}

}  // namespace
}  // namespace transport
