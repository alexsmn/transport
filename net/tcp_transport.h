#pragma once

#include "net/asio_transport.h"
#include "net/base/net_export.h"

namespace net {

class NET_EXPORT AsioTcpTransport final : public AsioTransport {
 public:
  AsioTcpTransport(boost::asio::io_context& io_context,
                   std::shared_ptr<const Logger> logger,
                   std::string host,
                   std::string service,
                   bool active);
  AsioTcpTransport(boost::asio::io_context& io_context,
                   std::shared_ptr<const Logger> logger,
                   boost::asio::ip::tcp::socket socket);
  ~AsioTcpTransport();

  int GetLocalPort() const;

  // Transport overrides
  virtual void Open(const Handlers& handlers) override;
  virtual std::string GetName() const override;
  virtual bool IsActive() const override { return active_; }

 private:
  boost::asio::io_context& io_context_;
  const std::shared_ptr<const Logger> logger_;
  const std::string host_;
  const std::string service_;
  const bool active_ = false;

  class ActiveCore;
  class PassiveCore;
};

}  // namespace net
