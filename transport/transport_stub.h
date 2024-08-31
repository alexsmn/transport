#pragma once

#include "transport/transport.h"

namespace transport {

class StubTransport : public Transport {
 public:
  explicit StubTransport(const executor& executor) : executor_{executor} {}

  virtual awaitable<error_code> open() override { co_return OK; }

  virtual awaitable<expected<any_transport>> accept() {
    co_return ERR_NOT_IMPLEMENTED;
  }

  virtual awaitable<error_code> close() override { co_return OK; }

  virtual awaitable<expected<size_t>> read(std::span<char> buffer) override {
    co_return 0;
  }

  virtual awaitable<expected<size_t>> write(
      std::span<const char> buffer) override {
    co_return buffer.size();
  }

  virtual std::string name() const override { return "StubTransport"; }
  virtual bool message_oriented() const override { return true; }
  virtual bool connected() const override { return false; }
  virtual bool active() const override { return true; }
  virtual executor get_executor() override { return executor_; }

 private:
  executor executor_;
};

}  // namespace transport
