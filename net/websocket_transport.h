#pragma once

#include "net/transport.h"

namespace net {

class WebSocketTransport final : public Transport {
 public:
  WebSocketTransport(const Executor& executor, std::string host, int port);
  ~WebSocketTransport();

  [[nodiscard]] virtual awaitable<Error> Open() override;
  [[nodiscard]] virtual awaitable<Error> Close() override;

  [[nodiscard]] virtual awaitable<ErrorOr<std::unique_ptr<Transport>>> Accept()
      override;

  [[nodiscard]] virtual awaitable<ErrorOr<size_t>> Read(
      std::span<char> data) override {
    co_return ERR_NOT_IMPLEMENTED;
  }

  [[nodiscard]] virtual awaitable<ErrorOr<size_t>> Write(
      std::span<const char> data) override {
    co_return data.size();
  }

  virtual std::string GetName() const override { return "WebSocket"; }
  virtual bool IsMessageOriented() const override { return true; }
  virtual bool IsConnected() const override { return false; }
  virtual bool IsActive() const override { return false; }
  virtual Executor GetExecutor() const override;

 private:
  class Core;
  class Connection;
  class ConnectionCore;

  std::shared_ptr<Core> core_;
};

}  // namespace net
