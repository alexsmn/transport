#pragma once

#include "net/base/net_errors.h"

#include <cassert>
#include <ostream>
#include <variant>

#define NET_ASSIGN_OR_RETURN(lhs, rexpr) \
  auto res = (rexpr);                    \
  if (!res.ok()) {                       \
    return res.error();                  \
  }                                      \
  lhs = std::move(res.value());

#define NET_ASSIGN_OR_CO_RETURN(lhs, rexpr) \
  auto res = (rexpr);                       \
  if (!res.ok()) {                          \
    co_return res.error();                  \
  }                                         \
  lhs = std::move(res.value());

#define NET_RETURN_IF_ERROR(rexpr)     \
  if (auto res = (rexpr); !res.ok()) { \
    return res.error();                \
  }

#define NET_CO_RETURN_IF_ERROR(rexpr)  \
  if (auto res = (rexpr); !res.ok()) { \
    co_return res.error();             \
  }

namespace net {

template <class T>
class [[nodiscard]] ErrorOr {
 public:
  ErrorOr(Error error) : value_{error} { assert(error != OK); }
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

  const T& value_or(const T& other_value) const& {
    return ok() ? value() : other_value;
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

  bool operator==(Error error) const { return this->error() == error; }

  bool operator==(const T& value) const {
    return ok() && this->value() == value;
  }

 private:
  std::variant<Error, T> value_;
};

}  // namespace net

template <class T>
inline std::ostream& operator<<(std::ostream& os, const net::ErrorOr<T>& st) {
  return st.ok() ? (os << *st) : (os << st.error());
}
