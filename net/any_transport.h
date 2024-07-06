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

  [[nodiscard]] std::string name() const {
    return transport_ ? transport_->GetName() : std::string{};
  }

  [[nodiscard]] bool message_oriented() const {
    return transport_ && transport_->IsMessageOriented();
  }

  [[nodiscard]] bool active() const {
    return transport_ && transport_->IsActive();
  }

  [[nodiscard]] Transport* get_impl() { return transport_.get(); }
  std::unique_ptr<Transport> release_impl() { return std::move(transport_); }

  [[nodiscard]] awaitable<Error> open() {
    if (!transport_) {
      co_return ERR_INVALID_HANDLE;
    }

    co_return co_await transport_->Open();
  }

  [[nodiscard]] awaitable<Error> close() {
    assert(transport_);

    if (!transport_) {
      co_return ERR_INVALID_HANDLE;
    }

    co_return co_await transport_->Close();
  }

  [[nodiscard]] awaitable<ErrorOr<any_transport>> accept() {
    if (!transport_) {
      co_return ERR_INVALID_HANDLE;
    }

    NET_ASSIGN_OR_CO_RETURN(auto transport, co_await transport_->Accept());

    co_return any_transport{std::move(transport)};
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

    co_return co_await transport_->Write(data);
  }

 private:
  std::unique_ptr<Transport> transport_;
};

}  // namespace net
