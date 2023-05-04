#pragma once

#include "net/transport.h"

#include <gmock/gmock.h>

namespace net {

class MockTransportDelegate : public net::Transport::Delegate {
 public:
  MOCK_METHOD(void, OnTransportOpened, (), (override));
  MOCK_METHOD(void, OnTransportClosed, (net::Error), (override));
  MOCK_METHOD(void, OnTransportDataReceived, (), (override));
  MOCK_METHOD(void,
              OnTransportMessageReceived,
              (std::span<const char> data),
              (override));
  MOCK_METHOD(net::Error,
              OnTransportAccepted,
              (std::unique_ptr<net::Transport> transport),
              (override));
};

}  // namespace net