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

  [[nodiscard]] Executor get_executor() const {
    return transport_ ? transport_->GetExecutor() : Executor{};
  }

  [[nodiscard]] awaitable<Error> open(const Transport::Handlers& handlers) {
    if (!transport_) {
      co_return ERR_INVALID_HANDLE;
    }

    co_return co_await transport_->Open(handlers);
  }

  void close() {
    assert(transport_);

    if (transport_) {
      transport_->Close();
    }
  }

  [[nodiscard]] awaitable<ErrorOr<size_t>> read(std::span<char> data) const {
    if (!transport_) {
      co_return ERR_INVALID_HANDLE;
    }

    co_return co_await transport_->Read(data);
  }

  [[nodiscard]] awaitable<ErrorOr<size_t>> write(
      std::span<const char> data) const {
    if (!transport_) {
      co_return ERR_INVALID_HANDLE;
    }

    co_return co_await transport_->Write(std::move(data));
  }

 private:
  std::unique_ptr<Transport> transport_;
};

}  // namespace net
