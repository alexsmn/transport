#pragma once

#include "net/transport.h"

#include <gmock/gmock.h>

namespace net {

class MockTransportHandlers {
 public:
  using AcceptHandler =
      testing::MockFunction<net::Error(std::unique_ptr<net::Transport>)>;

  AcceptHandler on_accept;

  Transport::Handlers AsHandlers() {
    return {.on_accept = on_accept.AsStdFunction()};
  }
};

}  // namespace net

namespace testing {

template <>
class StrictMock<net::MockTransportHandlers> {
 public:
  StrictMock<net::MockTransportHandlers::AcceptHandler> on_accept;

  net::Transport::Handlers AsHandlers() {
    return {.on_accept = on_accept.AsStdFunction()};
  }
};

}  // namespace testing
