#pragma once

#include "net/base/net_export.h"
#include "net/asio_transport.h"

#include <boost/asio.hpp>
#include <boost/circular_buffer.hpp>
#include <memory>

namespace net {

class NET_EXPORT AsioTcpTransport final : public AsioTransport {
 public:
  explicit AsioTcpTransport(boost::asio::io_context& io_context);
  AsioTcpTransport(boost::asio::io_context& io_context,
                   boost::asio::ip::tcp::socket socket);
  ~AsioTcpTransport();

  // Transport overrides
  virtual Error Open(Transport::Delegate& delegate) override;
  virtual std::string GetName() const override;
  virtual bool IsActive() const override { return active; }

  std::string host;
  std::string service;
  bool active = false;

 private:
  class ActiveCore;
  class PassiveCore;
};

}  // namespace net
