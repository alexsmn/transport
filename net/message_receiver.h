#pragma once

#include "net/transport.h"

#include <memory>
#include <vector>

namespace net {

class MessageReceiver {
 public:
  MessageReceiver(Transport& transport, size_t max_message_size)
      : transport_{transport}, max_message_size_{max_message_size} {}

  template <class H, class C>
  awaitable<void> Run(const H& handler, std::weak_ptr<C> cancelation) {
    for (;;) {
      buffer_.resize(max_message_size_);
      auto result = co_await transport_.Read(buffer_);
      if (cancelation.expired() || !result.ok() || *result == 0) {
        co_return;
      }
      buffer_.resize(*result);
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

}  // namespace net
