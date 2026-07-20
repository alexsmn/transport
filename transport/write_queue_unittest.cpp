#include "transport/write_queue.h"

#include "transport/any_transport.h"
#include "transport/transport_mock.h"

#include <gmock/gmock.h>

using namespace testing;

namespace transport {
namespace {

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

}  // namespace
}  // namespace transport
