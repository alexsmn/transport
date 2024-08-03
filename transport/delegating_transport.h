#pragma once

#include "transport/transport.h"

namespace transport {

// Intentionally non-final.
class DelegatingTransport : public Transport {
 public:
  explicit DelegatingTransport(Transport& delegate) : delegate_{delegate} {}

  [[nodiscard]] virtual awaitable<Error> Open() override {
    return delegate_.Open();
  }

  [[nodiscard]] virtual awaitable<ErrorOr<std::unique_ptr<Transport>>> Accept()
      override {
    return delegate_.Accept();
  }

  [[nodiscard]] awaitable<transport::Error> Close() override {
    return delegate_.Close();
  }

  [[nodiscard]] virtual awaitable<ErrorOr<size_t>> Read(
      std::span<char> data) override {
    return delegate_.Read(data);
  }

  [[nodiscard]] virtual awaitable<ErrorOr<size_t>> Write(
      std::span<const char> data) override {
    return delegate_.Write(data);
  }

  [[nodiscard]] virtual std::string GetName() const override {
    return delegate_.GetName();
  }

  [[nodiscard]] virtual bool IsMessageOriented() const override {
    return delegate_.IsMessageOriented();
  }

  [[nodiscard]] virtual bool IsConnected() const override {
    return delegate_.IsConnected();
  }

  [[nodiscard]] virtual bool IsActive() const override {
    return delegate_.IsActive();
  }

  [[nodiscard]] virtual Executor GetExecutor() const override {
    return delegate_.GetExecutor();
  }

 protected:
  Transport& delegate_;
};

}  // namespace transport