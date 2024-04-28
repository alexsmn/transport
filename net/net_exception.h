#pragma once

#include "net/base/net_errors.h"

#include <stdexcept>

namespace net {

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

}  // namespace net
