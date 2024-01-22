#pragma once

#include "net/asio_transport.h"
#include "net/base/net_export.h"

namespace net {

class NET_EXPORT AsioTcpTransport final : public AsioTransport {
 public:
  AsioTcpTransport(const Executor& executor,
                   std::shared_ptr<const Logger> logger,
                   std::string host,
                   std::string service,
                   bool active);

  // Uses `socket` executor.
  AsioTcpTransport(std::shared_ptr<const Logger> logger,
                   boost::asio::ip::tcp::socket socket);

  ~AsioTcpTransport();

  int GetLocalPort() const;

  // Transport overrides
  virtual promise<void> Open(const Handlers& handlers) override;
  virtual std::string GetName() const override;
  virtual bool IsActive() const override { return active_; }

 private:
  class ActiveCore;
  class PassiveCore;

  const bool active_;
};

}  // namespace net
