#pragma once

#include "net/transport.h"

namespace net {

class DelegatingTransport : public Transport {
 public:
  explicit DelegatingTransport(Transport& delegate) : delegate_{delegate} {}

  virtual promise<void> Open(const Handlers& handlers) override {
    return delegate_.Open(handlers);
  }

  virtual void Close() override { delegate_.Close(); }

  virtual int Read(std::span<char> data) override {
    return delegate_.Read(data);
  }

  virtual promise<size_t> Write(std::span<const char> data) override {
    return delegate_.Write(data);
  }

  virtual std::string GetName() const override { return delegate_.GetName(); }

  virtual bool IsMessageOriented() const override {
    return delegate_.IsMessageOriented();
  }

  virtual bool IsConnected() const override { return delegate_.IsConnected(); }

  virtual bool IsActive() const override { return delegate_.IsActive(); }

 protected:
  Transport& delegate_;
};

}  // namespace net