#pragma once

#include "transport/transport.h"

namespace transport {

class StubTransport : public Transport {
 public:
  explicit StubTransport(const Executor& executor) : executor_{executor} {}

  virtual awaitable<Error> open() override { co_return OK; }

  virtual awaitable<ErrorOr<std::unique_ptr<Transport>>> accept() {
    co_return ERR_NOT_IMPLEMENTED;
  }

  virtual awaitable<Error> close() override { co_return OK; }

  virtual awaitable<ErrorOr<size_t>> read(std::span<char> buffer) override {
    co_return 0;
  }

  virtual awaitable<ErrorOr<size_t>> write(
      std::span<const char> buffer) override {
    co_return buffer.size();
  }

  virtual std::string name() const override { return "StubTransport"; }
  virtual bool message_oriented() const override { return true; }
  virtual bool connected() const override { return false; }
  virtual bool active() const override { return true; }
  virtual Executor get_executor() const override { return executor_; }

 private:
  Executor executor_;
};

}  // namespace transport
