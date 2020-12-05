#pragma once

#include "net/asio_transport.h"
#include "net/udp_socket_factory.h"

namespace net {

class AsioUdpTransport final : public AsioTransport {
 public:
  explicit AsioUdpTransport(boost::asio::io_context& io_context,
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
  const UdpSocketFactory udp_socket_factory_;

  class UdpCore;
  class UdpPassiveCore;
  class AcceptedTransport;
};

}  // namespace net
