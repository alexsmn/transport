#pragma once

#include "transport/transport.h"

#include <memory>

namespace transport {

class any_transport {
 public:
  any_transport() = default;

  explicit any_transport(std::unique_ptr<Transport> transport)
      : transport_{std::move(transport)} {}

  any_transport(any_transport&&) = default;
  any_transport& operator=(any_transport&&) = default;

  explicit operator bool() const { return transport_ != nullptr; }

  [[nodiscard]] Executor get_executor() const {
    return transport_ ? transport_->get_executor() : Executor{};
  }

  [[nodiscard]] std::string name() const {
    return transport_ ? transport_->name() : std::string{};
  }

  [[nodiscard]] bool message_oriented() const {
    return transport_ && transport_->message_oriented();
  }

  [[nodiscard]] bool active() const {
    return transport_ && transport_->active();
  }

  [[nodiscard]] bool connected() const {
    return transport_ && transport_->connected();
  }

  [[nodiscard]] Transport* get_impl() { return transport_.get(); }
  std::unique_ptr<Transport> release_impl() { return std::move(transport_); }

  [[nodiscard]] awaitable<Error> open() {
    if (!transport_) {
      co_return ERR_INVALID_HANDLE;
    }

    co_return co_await transport_->open();
  }

  [[nodiscard]] awaitable<Error> close() {
    assert(transport_);

    if (!transport_) {
      co_return ERR_INVALID_HANDLE;
    }

    co_return co_await transport_->close();
  }

  [[nodiscard]] awaitable<ErrorOr<any_transport>> accept() {
    if (!transport_) {
      co_return ERR_INVALID_HANDLE;
    }

    NET_ASSIGN_OR_CO_RETURN(auto transport, co_await transport_->accept());

    co_return any_transport{std::move(transport)};
  }

  [[nodiscard]] awaitable<ErrorOr<size_t>> read(std::span<char> data) const {
    if (!transport_) {
      co_return ERR_INVALID_HANDLE;
    }

    co_return co_await transport_->read(data);
  }

  [[nodiscard]] awaitable<ErrorOr<size_t>> write(
      std::span<const char> data) const {
    if (!transport_) {
      co_return ERR_INVALID_HANDLE;
    }

    co_return co_await transport_->write(data);
  }

 private:
  std::unique_ptr<Transport> transport_;
};

}  // namespace transport
