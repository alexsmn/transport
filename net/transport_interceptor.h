#pragma once

#include "net/error_or.h"

#include <optional>
#include <span>

namespace net {

class TransportInterceptor {
 public:
  virtual ~TransportInterceptor() = default;

  virtual std::optional<ErrorOr<size_t>> InterceptWrite(
      std::span<const char> data) const {
    return std::nullopt;
  }
};

}  // namespace net
