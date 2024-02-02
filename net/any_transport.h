#pragma once

#include "net/transport.h"

#include <memory>

namespace net {

class any_transport {
 public:
  any_transport() = default;

  explicit any_transport(std::unique_ptr<Transport> transport)
      : transport_{std::move(transport)} {}

  any_transport(any_transport&&) = default;
  any_transport& operator=(any_transport&&) = default;

  explicit operator bool() const { return transport_ != nullptr; }

  promise<void> open(const Transport::Handlers& handlers) {
    return transport_ ? transport_->Open(handlers)
                      : make_error_promise<void>(ERR_INVALID_HANDLE);
  }

  void close() {
    assert(transport_);

    if (transport_) {
      transport_->Close();
    }
  }

  int read(std::vector<char> data) const {
    return transport_ ? transport_->Read(data) : ERR_INVALID_HANDLE;
  }

  promise<size_t> write(std::span<const char> data) const {
    return transport_ ? transport_->Write(data)
                      : make_error_promise<size_t>(ERR_INVALID_HANDLE);
  }

 private:
  std::unique_ptr<Transport> transport_;
};

}  // namespace net
