#pragma once

#include "net/asio_transport.h"
#include "net/udp_socket_factory.h"

namespace net {

class Logger;

class AsioUdpTransport final : public AsioTransport {
 public:
  AsioUdpTransport(std::shared_ptr<const Logger> logger,
                   UdpSocketFactory udp_socket_factory);

  // Transport overrides
  virtual Error Open(Transport::Delegate& delegate) override;
  virtual std::string GetName() const override;
  virtual bool IsActive() const override { return active; }
  virtual bool IsMessageOriented() const override { return true; }

  std::string host;
  std::string service;
  bool active = false;

 private:
  const std::shared_ptr<const Logger> logger_;
  const UdpSocketFactory udp_socket_factory_;

  class UdpActiveCore;
  class UdpPassiveCore;
  class AcceptedTransport;
};

}  // namespace net
