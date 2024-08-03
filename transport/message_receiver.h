#pragma once

#include "transport/any_transport.h"
#include "transport/message_utils.h"
#include "transport/transport.h"

#include <memory>
#include <vector>

namespace transport {

class MessageReceiver {
 public:
  MessageReceiver(any_transport& transport, size_t max_message_size)
      : transport_{*transport.get_impl()},
        max_message_size_{max_message_size} {}

  MessageReceiver(Transport& transport, size_t max_message_size)
      : transport_{transport}, max_message_size_{max_message_size} {}

  MessageReceiver(const MessageReceiver&) = delete;
  MessageReceiver& operator=(const MessageReceiver&) = delete;

  template <class H, class C>
  awaitable<void> Run(const H& handler, std::weak_ptr<C> cancelation) {
    for (;;) {
      auto error = co_await ReadMessage(transport_, max_message_size_, buffer_);
      if (cancelation.expired() || error != OK || buffer_.empty()) {
        co_return;
      }
      handler(buffer_);
    }
  }

  template <class H>
  awaitable<void> Run(const H& handler) {
    auto cancelation = std::make_shared<bool>();
    co_await Run(handler, std::weak_ptr{cancelation});
  }

 private:
  Transport& transport_;
  size_t max_message_size_;

  std::vector<char> buffer_;
};

}  // namespace transport
