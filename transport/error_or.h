#pragma once

#include "transport/error.h"

#include <cassert>
#include <ostream>
#include <variant>

#define CONCAT(x, y) CONCAT_IMPL(x, y)
#define CONCAT_IMPL(x, y) x##y

#define NET_ASSIGN_OR_RETURN(lhs, rexpr) \
  NET_ASSIGN_OR_RETURN_IMPL(lhs, rexpr, CONCAT(res, __LINE__), return)

#define NET_ASSIGN_OR_CO_RETURN(lhs, rexpr) \
  NET_ASSIGN_OR_RETURN_IMPL(lhs, rexpr, CONCAT(res, __LINE__), co_return)

#define NET_ASSIGN_OR_RETURN_IMPL(lhs, rexpr, res, return_impl)             \
  auto res = (rexpr);                                                       \
  if (!res.ok()) {                                                          \
    static constexpr ::boost::source_location loc = BOOST_CURRENT_LOCATION; \
    return_impl ::transport::Error{res.error(), &loc};                            \
  }                                                                         \
  lhs = std::move(res.value());

#define NET_RETURN_IF_ERROR(rexpr) NET_RETURN_IF_ERROR_IMPL(rexpr, return)
#define NET_CO_RETURN_IF_ERROR(rexpr) NET_RETURN_IF_ERROR_IMPL(rexpr, co_return)

#define NET_RETURN_IF_ERROR_IMPL(rexpr, return_impl)                        \
  if (auto err = (rexpr); err != transport::OK) {                                 \
    static constexpr ::boost::source_location loc = BOOST_CURRENT_LOCATION; \
    return_impl ::transport::Error{err, &loc};                                    \
  }

namespace transport {

template <class T>
class [[nodiscard]] ErrorOr {
 public:
  ErrorOr(Error error) : value_{error} { assert(error != OK); }

  template <class E>
    requires boost::system::is_error_code_enum<E>::value
  ErrorOr(E ec) : value_{Error{ec}} {
    assert(ec != boost::system::errc::success);
  }

  ErrorOr(T value) : value_{std::move(value)} {}

  bool ok() const { return std::holds_alternative<T>(value_); }

  Error error() const {
    const auto* error = std::get_if<Error>(&value_);
    return error ? *error : OK;
  }

  T& value() & {
    assert(ok());
    return std::get<T>(value_);
  }

  T&& value() && {
    assert(ok());
    return std::move(std::get<T>(value_));
  }

  const T& value() const& {
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

}  // namespace transport

template <class T>
inline std::ostream& operator<<(std::ostream& os, const transport::ErrorOr<T>& st) {
  return st.ok() ? (os << *st) : (os << st.error());
}
