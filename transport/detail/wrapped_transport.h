#pragma once

#include "transport/transport.h"

namespace transport::detail {

template <typename T>
class WrappedTransport final : public Transport {
 public:
  template <class U>
  explicit WrappedTransport(U&& transport)
      : impl_{std::forward<U>(transport)} {}

  executor get_executor() override { return impl_.get_executor(); }

  std::string name() const override { return impl_.name(); }

  bool message_oriented() const override { return impl_.message_oriented(); }

  bool active() const override { return impl_.active(); }

  bool connected() const override { return impl_.connected(); }

  awaitable<error_code> open() override { co_return co_await impl_.open(); }

  awaitable<error_code> close() override { co_return co_await impl_.close(); }

  awaitable<expected<size_t>> read(std::span<char> data) override {
    co_return co_await impl_.read(data);
  }

  awaitable<expected<size_t>> write(std::span<const char> data) override {
    co_return co_await impl_.write(data);
  }

  awaitable<expected<any_transport*>> accept() override {
    return impl_.accept();
  }

 private:
  T impl_;
};

}  // namespace transport::detail
