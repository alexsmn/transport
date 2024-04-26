#pragma once

#include "net/asio_transport.h"
#include "net/udp_socket_factory.h"

namespace net {

class Logger;

class AsioUdpTransport final : public AsioTransport {
 public:
  AsioUdpTransport(const Executor& executor,
                   std::shared_ptr<const Logger> logger,
                   UdpSocketFactory udp_socket_factory,
                   std::string host,
                   std::string service,
                   bool active);

  // Transport overrides
  [[nodiscard]] virtual boost::asio::awaitable<void> Open(
      Handlers handlers) override;

  virtual std::string GetName() const override;
  virtual bool IsActive() const override { return active_; }
  virtual bool IsMessageOriented() const override { return true; }

 private:
  class UdpActiveCore;
  class UdpPassiveCore;
  class AcceptedTransport;

  const bool active_;
};

}  // namespace net
