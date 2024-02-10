#pragma once

#include "net/base/net_export.h"
#include "net/logger.h"
#include "net/transport_factory.h"
#include "net/udp_socket_factory.h"

namespace boost::asio {
class io_context;
}

namespace net {

class InprocessTransportHost;

class NET_EXPORT TransportFactoryImpl : public TransportFactory {
 public:
  explicit TransportFactoryImpl(boost::asio::io_context& io_context);
  ~TransportFactoryImpl();

  // Returns nullptr if parameters are invalid.
  virtual std::unique_ptr<Transport> CreateTransport(
      const TransportString& transport_string,
      const net::Executor& executor,
      std::shared_ptr<const Logger> logger =
          net::NullLogger::GetInstance()) override;

 private:
  boost::asio::io_context& io_context_;
  UdpSocketFactory udp_socket_factory_;
  std::unique_ptr<InprocessTransportHost> inprocess_transport_host_;
};

std::shared_ptr<TransportFactory> CreateTransportFactory();

}  // namespace net
