#pragma once

#include "transport/transport.h"

namespace transport {

class WebSocketTransport final : public Transport {
 public:
  WebSocketTransport(const Executor& executor, std::string host, int port);
  ~WebSocketTransport();

  [[nodiscard]] virtual awaitable<Error> open() override;
  [[nodiscard]] virtual awaitable<Error> close() override;

  [[nodiscard]] virtual awaitable<ErrorOr<std::unique_ptr<Transport>>> accept()
      override;

  [[nodiscard]] virtual awaitable<ErrorOr<size_t>> read(
      std::span<char> data) override {
    co_return ERR_NOT_IMPLEMENTED;
  }

  [[nodiscard]] virtual awaitable<ErrorOr<size_t>> write(
      std::span<const char> data) override {
    co_return data.size();
  }

  virtual std::string name() const override { return "WebSocket"; }
  virtual bool message_oriented() const override { return true; }
  virtual bool connected() const override { return false; }
  virtual bool active() const override { return false; }
  virtual Executor get_executor() const override;

 private:
  class Core;
  class Connection;
  class ConnectionCore;

  std::shared_ptr<Core> core_;
};

}  // namespace transport
