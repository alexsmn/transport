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

  virtual Error Open(Delegate& delegate) override;
  virtual void Close() override;
  virtual int Read(void* data, size_t len) override { return 0; }
  virtual int Write(const void* data, size_t len) override { return 0; }
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
