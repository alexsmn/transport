#pragma once

#include "net/transport.h"

namespace net {

// Intentionally non-final.
class DelegatingTransport : public Transport {
 public:
  explicit DelegatingTransport(Transport& delegate) : delegate_{delegate} {}

  [[nodiscard]] virtual awaitable<Error> Open(Handlers handlers) override {
    return delegate_.Open(std::move(handlers));
  }

  virtual void Close() override { delegate_.Close(); }

  [[nodiscard]] virtual int Read(std::span<char> data) override {
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

}  // namespace net