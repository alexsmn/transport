#pragma once

#include <gmock/gmock.h>

#include "net/transport.h"

namespace net {

class TransportDelegateMock : public net::Transport::Delegate {
 public:
  MOCK_METHOD0(OnTransportOpened, void());
  MOCK_METHOD1(OnTransportClosed, void(net::Error));
  MOCK_METHOD0(OnTransportDataReceived, void());
  MOCK_METHOD2(OnTransportMessageReceived, void(const void* data, size_t size));
  MOCK_METHOD1(OnTransportAccepted, net::Error(std::unique_ptr<net::Transport> transport));
};

} // namespace net