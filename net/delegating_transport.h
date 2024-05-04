#pragma once

#include "net/transport.h"

namespace net {

// Intentionally non-final.
class DelegatingTransport : public Transport {
 public:
  explicit DelegatingTransport(Transport& delegate) : delegate_{delegate} {}

  virtual awaitable<void> Open(Handlers handlers) override {
    return delegate_.Open(std::move(handlers));
  }

  virtual void Close() override { delegate_.Close(); }

  virtual int Read(std::span<char> data) override {
    return delegate_.Read(data);
  }

  virtual awaitable<size_t> Write(std::vector<char> data) override {
    return delegate_.Write(std::move(data));
  }

  virtual std::string GetName() const override { return delegate_.GetName(); }

  virtual bool IsMessageOriented() const override {
    return delegate_.IsMessageOriented();
  }

  virtual bool IsConnected() const override { return delegate_.IsConnected(); }

  virtual bool IsActive() const override { return delegate_.IsActive(); }

  virtual Executor GetExecutor() const override {
    return delegate_.GetExecutor();
  }

 protected:
  Transport& delegate_;
};

}  // namespace net