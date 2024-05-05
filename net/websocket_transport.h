#pragma once

#include "net/transport.h"

namespace net {

class WebSocketTransport final : public Transport {
 public:
  WebSocketTransport(const Executor& executor, std::string host, int port);
  ~WebSocketTransport();

  [[nodiscard]] virtual awaitable<Error> Open(Handlers handlers) override;

  virtual void Close() override;
  virtual int Read(std::span<char> data) override { return 0; }

  [[nodiscard]] virtual awaitable<ErrorOr<size_t>> Write(
      std::vector<char> data) override {
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
