#pragma once

#include "transport/error_or.h"

#include <optional>
#include <span>

namespace transport {

class TransportInterceptor {
 public:
  virtual ~TransportInterceptor() = default;

  virtual std::optional<ErrorOr<size_t>> InterceptWrite(
      std::span<const char> data) const {
    return std::nullopt;
  }
};

}  // namespace transport
