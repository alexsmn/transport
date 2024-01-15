#pragma once

#include "net/asio_transport.h"
#include "net/udp_socket_factory.h"

namespace net {

class Logger;

class AsioUdpTransport final : public AsioTransport {
 public:
  AsioUdpTransport(std::shared_ptr<const Logger> logger,
                   UdpSocketFactory udp_socket_factory,
                   std::string host,
                   std::string service,
                   bool active);

  // Transport overrides
  virtual promise<void> Open(const Handlers& handlers) override;
  virtual std::string GetName() const override;
  virtual bool IsActive() const override { return active_; }
  virtual bool IsMessageOriented() const override { return true; }

 private:
  const std::shared_ptr<const Logger> logger_;
  const UdpSocketFactory udp_socket_factory_;
  const std::string host_;
  const std::string service_;
  const bool active_ = false;

  class UdpActiveCore;
  class UdpPassiveCore;
  class AcceptedTransport;
};

}  // namespace net
