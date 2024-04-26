#pragma once

#include "net/transport.h"

namespace boost::asio {
class io_context;
}

namespace net {

class WebSocketTransport final : public Transport {
 public:
  WebSocketTransport(boost::asio::io_context& io_context,
                     std::string host,
                     int port);
  ~WebSocketTransport();

  [[nodiscard]] virtual boost::asio::awaitable<void> Open(
      Handlers handlers) override;

  virtual void Close() override;
  virtual int Read(std::span<char> data) override { return 0; }

  [[nodiscard]] virtual boost::asio::awaitable<size_t> Write(
      std::vector<char> data) override {
    co_return data.size();
  }

  virtual std::string GetName() const override { return "WebSocket"; }
  virtual bool IsMessageOriented() const override { return true; }
  virtual bool IsConnected() const override { return false; }
  virtual bool IsActive() const override { return false; }

 private:
  class Core;
  class Connection;
  class ConnectionCore;

  std::shared_ptr<Core> core_;
};

}  // namespace net
