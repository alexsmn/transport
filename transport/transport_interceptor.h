#pragma once

#include "transport/expected.h"

#include <optional>
#include <span>

namespace transport {

class TransportInterceptor {
 public:
  virtual ~TransportInterceptor() = default;

  virtual std::optional<expected<size_t>> InterceptWrite(
      std::span<const char> data) const {
    return std::nullopt;
  }
};

}  // namespace transport
