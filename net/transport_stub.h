#pragma once

#include "net/transport.h"

namespace net {

class StubTransport : public Transport {
 public:
  explicit StubTransport(const Executor& executor) : executor_{executor} {}

  virtual awaitable<Error> Open(Handlers handlers) override {
    co_return net::OK;
  }

  virtual void Close() override {}

  virtual awaitable<ErrorOr<size_t>> Read(std::span<char> buffer) override {
    co_return 0;
  }

  virtual awaitable<ErrorOr<size_t>> Write(
      std::span<const char> buffer) override {
    co_return buffer.size();
  }

  virtual std::string GetName() const override { return "StubTransport"; }
  virtual bool IsMessageOriented() const override { return true; }
  virtual bool IsConnected() const override { return false; }
  virtual bool IsActive() const override { return true; }
  virtual Executor GetExecutor() const override { return executor_; }

 private:
  Executor executor_;
};

}  // namespace net
