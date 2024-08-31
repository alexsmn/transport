#pragma once

#include "transport/executor.h"
#include "transport/log.h"
#include "transport/transport_factory.h"
#include "transport/udp_socket_factory.h"

namespace boost::asio {
class io_context;
}

namespace transport {

class InprocessTransportHost;

class TransportFactoryImpl : public TransportFactory {
 public:
  explicit TransportFactoryImpl(boost::asio::io_context& io_context);
  ~TransportFactoryImpl();

  // Returns nullptr if parameters are invalid.
  virtual expected<any_transport> CreateTransport(
      const TransportString& transport_string,
      const Executor& executor,
      const log_source& log = {}) override;

 private:
  boost::asio::io_context& io_context_;
  UdpSocketFactory udp_socket_factory_;
  std::unique_ptr<InprocessTransportHost> inprocess_transport_host_;
};

std::shared_ptr<TransportFactory> CreateTransportFactory();

}  // namespace transport
