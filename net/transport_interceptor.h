#pragma once

#include "net/promise.h"

#include <optional>
#include <span>

namespace net {

class TransportInterceptor {
 public:
  virtual std::optional<promise<size_t>> InterceptWrite(
      std::span<const char> data) const {
    return std::nullopt;
  }
};

}  // namespace net
