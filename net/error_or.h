#pragma once

#include "net/base/net_errors.h"

#include <cassert>
#include <ostream>
#include <variant>

namespace net {

template <class T>
class [[nodiscard]] ErrorOr {
 public:
  ErrorOr(Error error) : value_{std::move(error)} { assert(!this->error()); }
  ErrorOr(T value) : value_{std::move(value)} {}

  bool ok() const { return std::holds_alternative<T>(value_); }

  Error error() const {
    const auto* error = std::get_if<Error>(&value_);
    return error ? *error : OK;
  }

  T& value() {
    assert(ok());
    return std::get<T>(value_);
  }

  const T& value() const {
    assert(ok());
    return std::get<T>(value_);
  }

  T& operator*() { return value(); }

  const T& operator*() const { return value(); }

  T* operator->() {
    assert(ok());
    return &std::get<T>(value_);
  }

  const T* operator->() const {
    assert(ok());
    return &std::get<T>(value_);
  }

 private:
  std::variant<Error, T> value_;
};

}  // namespace net

template <class T>
inline std::ostream& operator<<(std::ostream& os, const net::ErrorOr<T>& st) {
  return st.ok() ? (os << *st) : (os << st.error());
}
