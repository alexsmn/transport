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

}  // namespace transport