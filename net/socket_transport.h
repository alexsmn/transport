#pragma once

#include "net/base/net_export.h"
#include "net/socket_delegate.h"
#include "net/transport.h"

namespace net {

class NET_EXPORT SocketTransport : public Transport,
                                   protected SocketDelegate {
 public:
  SocketTransport();
  // Takes ownership on |socket|.
  explicit SocketTransport(std::unique_ptr<Socket> socket);
  virtual ~SocketTransport();
  
  void set_active(bool active) { active_ = active; }

  // Transport overrides
  virtual Error Open() override;
  virtual void Close() override;
  virtual int Read(void* data, size_t len) override;
  virtual int Write(const void* data, size_t len) override;
  virtual std::string GetName() const override;
  virtual bool IsMessageOriented() const override { return false; }
  virtual bool IsConnected() const override { return connected_; }
  virtual bool IsActive() const override { return active_; }

  std::string host_;
  unsigned short port_;

 protected:
  // Socket::Delegate overrides
  virtual void OnSocketConnected(Error error) override;
  virtual void OnSocketAccepted(std::unique_ptr<Socket> socket) override;
  virtual void OnSocketClosed(Error error) override;
  virtual void OnSocketDataReceived() override;

 private:
  std::unique_ptr<Socket> socket_;
  bool connected_ = false;
  bool active_ = true;
};

} // namespace net
