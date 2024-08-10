#pragma once

#include "transport/asio_transport.h"
#include "transport/udp_socket_factory.h"

namespace transport {

class Logger;

class AsioUdpTransport final : public AsioTransport {
 public:
  AsioUdpTransport(const Executor& executor,
                   const log_source& log,
                   UdpSocketFactory udp_socket_factory,
                   std::string host,
                   std::string service,
                   bool active);

  explicit AsioUdpTransport(std::shared_ptr<Core> core);

  // Transport overrides
  [[nodiscard]] virtual awaitable<Error> open() override;
  [[nodiscard]] virtual awaitable<ErrorOr<any_transport>> accept() override;

  virtual std::string name() const override;
  virtual bool active() const override { return active_; }
  virtual bool message_oriented() const override { return true; }

 private:
  class UdpActiveCore;
  class UdpPassiveCore;
  class UdpAcceptedCore;

  const bool active_;
};

}  // namespace transport
