#pragma once

#include "transport/transport.h"

namespace transport {

// Intentionally non-final.
class DelegatingTransport : public Transport {
 public:
  explicit DelegatingTransport(any_transport& delegate) : delegate_{delegate} {}

  [[nodiscard]] virtual awaitable<Error> open() override {
    return delegate_.open();
  }

  [[nodiscard]] virtual awaitable<ErrorOr<any_transport>> accept() override {
    return delegate_.accept();
  }

  [[nodiscard]] awaitable<transport::Error> close() override {
    return delegate_.close();
  }

  [[nodiscard]] virtual awaitable<ErrorOr<size_t>> read(
      std::span<char> data) override {
    return delegate_.read(data);
  }

  [[nodiscard]] virtual awaitable<ErrorOr<size_t>> write(
      std::span<const char> data) override {
    return delegate_.write(data);
  }

  [[nodiscard]] virtual std::string name() const override {
    return delegate_.name();
  }

  [[nodiscard]] virtual bool message_oriented() const override {
    return delegate_.message_oriented();
  }

  [[nodiscard]] virtual bool connected() const override {
    return delegate_.connected();
  }

  [[nodiscard]] virtual bool active() const override {
    return delegate_.active();
  }

  [[nodiscard]] virtual Executor get_executor() override {
    return delegate_.get_executor();
  }

 protected:
  any_transport& delegate_;
};

}  // namespace transport