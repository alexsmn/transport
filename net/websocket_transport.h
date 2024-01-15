#pragma once

#include "net/transport.h"

namespace boost::asio {
class io_context;
}

namespace net {

class WebSocketTransport : public Transport {
 public:
  WebSocketTransport(boost::asio::io_context& io_context,
                     std::string host,
                     int port);
  ~WebSocketTransport();

  virtual promise<void> Open(const Handlers& handlers) override;
  virtual void Close() override;
  virtual int Read(std::span<char> data) override { return 0; }
  virtual promise<size_t> Write(std::span<const char> data) override {
    return make_resolved_promise<size_t>(data.size());
  }
  virtual std::string GetName() const override { return "Websocket"; }
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
