#pragma once

#include <promise.hpp/promise.hpp>

namespace net {

template <class T = void>
using promise = promise_hpp::promise<T>;

class net_exception : public std::exception {
 public:
  explicit net_exception(Error error) : error_(error) {}

  Error error() const { return error_; }

 private:
  const Error error_;
};

inline Error get_error(const std::exception_ptr& e) {
  try {
    std::rethrow_exception(e);
  } catch (const net_exception& e) {
    return e.error();
  } catch (...) {
    return Error::ERR_FAILED;
  }
}

inline promise<> make_resolved_promise() {
  return promise_hpp::make_resolved_promise();
}

template <class T>
inline auto make_resolved_promise(T&& value) {
  return promise_hpp::make_resolved_promise<T>(std::forward<T>(value));
}

inline promise<> make_error_promise(Error error) {
  return promise_hpp::make_rejected_promise(net_exception{error});
}

template <class T>
inline promise<T> make_error_promise(Error error) {
  return promise_hpp::make_rejected_promise<T>(net_exception{error});
}

}  // namespace net
