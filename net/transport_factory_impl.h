#pragma once

#include "net/base/net_export.h"
#include "net/transport_factory.h"
#include "net/udp_socket_factory.h"

namespace boost::asio {
class io_context;
}

namespace net {

class NET_EXPORT TransportFactoryImpl : public TransportFactory {
 public:
  explicit TransportFactoryImpl(boost::asio::io_context& io_context);

  // Returns nullptr if parameters are invalid.
  virtual std::unique_ptr<Transport> CreateTransport(
      const TransportString& transport_string,
      std::shared_ptr<const Logger> logger) override;

 private:
  boost::asio::io_context& io_context_;
  UdpSocketFactory udp_socket_factory_;
};

std::shared_ptr<TransportFactory> CreateTransportFactory();

}  // namespace net
