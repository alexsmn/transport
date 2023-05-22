#pragma once

#include "net/transport.h"

#include <gmock/gmock.h>

namespace net {

class MockTransportHandlers {
 public:
  using OpenHandler = testing::MockFunction<void()>;
  using CloseHandler = testing::MockFunction<void(net::Error)>;
  using DataHandler = testing::MockFunction<void()>;
  using MessageHandler = testing::MockFunction<void(std::span<const char>)>;
  using AcceptHandler =
      testing::MockFunction<net::Error(std::unique_ptr<net::Transport>)>;

  OpenHandler on_open;
  CloseHandler on_close;
  DataHandler on_data;
  MessageHandler on_message;
  AcceptHandler on_accept;

  Transport::Handlers AsHandlers() {
    return {.on_open = on_open.AsStdFunction(),
            .on_close = on_close.AsStdFunction(),
            .on_data = on_data.AsStdFunction(),
            .on_message = on_message.AsStdFunction(),
            .on_accept = on_accept.AsStdFunction()};
  }
};

}  // namespace net

namespace testing {

template <>
class StrictMock<net::MockTransportHandlers> {
 public:
  StrictMock<net::MockTransportHandlers::OpenHandler> on_open;
  StrictMock<net::MockTransportHandlers::CloseHandler> on_close;
  StrictMock<net::MockTransportHandlers::DataHandler> on_data;
  StrictMock<net::MockTransportHandlers::MessageHandler> on_message;
  StrictMock<net::MockTransportHandlers::AcceptHandler> on_accept;

  net::Transport::Handlers AsHandlers() {
    return {.on_open = on_open.AsStdFunction(),
            .on_close = on_close.AsStdFunction(),
            .on_data = on_data.AsStdFunction(),
            .on_message = on_message.AsStdFunction(),
            .on_accept = on_accept.AsStdFunction()};
  }
};

}  // namespace testing
