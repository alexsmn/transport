#pragma once

#include "net/base/net_errors.h"
#include "net/socket.h"

namespace net {

class SocketDelegate {
 public:
  // Accept incoming connection
  virtual void OnSocketAccepted(std::unique_ptr<Socket> socket) {}

  // Data available in receive buffer for reading.
  virtual void OnSocketDataReceived() {}

  // Socket closed by remote side. |error| is close reason.
  virtual void OnSocketClosed(Error error) {}

  // Connection established.
  virtual void OnSocketConnected(Error error) {}

  // Send possible. There is empty place in send buffer.
  virtual void OnSocketSendPossible() {}
};

} // namespace net
