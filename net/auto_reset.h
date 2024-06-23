#pragma once

#include <utility>

namespace net {

template <typename T>
class AutoReset {
 public:
  AutoReset(T& var, T new_value) : var_{var}, old_value_{std::move(var)} {
    var_ = std::move(new_value);
  }

  ~AutoReset() { var_ = std::move(old_value_); }

  AutoReset(const AutoReset&) = delete;
  AutoReset& operator=(const AutoReset&) = delete;

 private:
  T& var_;
  T old_value_;
};

}  // namespace net
