#pragma once

#include "transport/transport.h"

namespace transport {

class StubTransport : public Transport {
 public:
  explicit StubTransport(const Executor& executor) : executor_{executor} {}

  virtual awaitable<Error> Open() override { co_return OK; }

  virtual awaitable<ErrorOr<std::unique_ptr<Transport>>> Accept() {
    co_return ERR_NOT_IMPLEMENTED;
  }

  virtual awaitable<Error> Close() override { co_return OK; }

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

}  // namespace transport
