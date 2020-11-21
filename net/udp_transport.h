#pragma once

#include "net/asio_transport.h"
#include "net/base/net_export.h"

namespace net {

class NET_EXPORT AsioUdpTransport final : public AsioTransport {
 public:
  explicit AsioUdpTransport(boost::asio::io_context& io_context);
  ~AsioUdpTransport();

  // Transport overrides
  virtual Error Open(Transport::Delegate& delegate) override;
  virtual std::string GetName() const override;
  virtual bool IsActive() const override { return active; }
  virtual bool IsMessageOriented() const override { return true; }

  std::string host;
  std::string service;
  bool active = false;

 private:
  class UdpCore;
  class UdpPassiveCore;
  class AcceptedTransport;
};

}  // namespace net
