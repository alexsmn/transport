#pragma once

#include "transport/transport.h"

namespace transport {

class WebSocketTransport final : public Transport {
 public:
  WebSocketTransport(const executor& executor, std::string host, int port);
  ~WebSocketTransport();

  [[nodiscard]] virtual awaitable<error_code> open() override;
  [[nodiscard]] virtual awaitable<error_code> close() override;
  [[nodiscard]] virtual awaitable<expected<any_transport>> accept() override;

  [[nodiscard]] virtual awaitable<expected<size_t>> read(
      std::span<char> data) override {
    co_return ERR_NOT_IMPLEMENTED;
  }

  [[nodiscard]] virtual awaitable<expected<size_t>> write(
      std::span<const char> data) override {
    co_return data.size();
  }

  virtual std::string name() const override { return "WebSocket"; }
  virtual bool message_oriented() const override { return true; }
  virtual bool connected() const override { return false; }
  virtual bool active() const override { return false; }
  virtual executor get_executor() override;

 private:
  class Core;
  class Connection;
  class ConnectionCore;

  std::shared_ptr<Core> core_;
};

}  // namespace transport
