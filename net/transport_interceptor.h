#pragma once

#include <optional>
#include <span>

namespace net {

class TransportInterceptor {
 public:
  virtual ~TransportInterceptor() = default;

  virtual std::optional<size_t> InterceptWrite(
      std::span<const char> data) const {
    return std::nullopt;
  }
};

}  // namespace net
