#pragma once

#include "net/transport.h"

#include <memory>

namespace net {

class opened_transport {
 public:
  opened_transport() {}

  explicit opened_transport(std::unique_ptr<Transport> transport)
      : transport_{std::move(transport)} {}

  opened_transport(opened_transport&&) = default;
  opened_transport& operator=(opened_transport&&) = default;

  explicit operator bool() const { return transport_ != nullptr; }

  void close() {
    if (transport_) {
      transport_->Close();
      transport_.reset();
    }
  }

  promise<std::vector<char>> read(std::vector<char> data) const {
    if (!transport_) {
      return make_error_promise<std::vector<char>>(ERR_INVALID_HANDLE);
    }

    int result = transport_->Read(data);
    if (result < 0) {
      return make_error_promise<std::vector<char>>(static_cast<Error>(result));
    }

    data.resize(result);
    return make_resolved_promise(std::move(data));
  }

  promise<size_t> write(const std::string& message) const {
    return transport_ ? transport_->Write(message)
                      : make_error_promise<size_t>(ERR_INVALID_HANDLE);
  }

 private:
  std::unique_ptr<Transport> transport_;
};

}  // namespace net
