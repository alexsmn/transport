#pragma once

#include "net/transport_factory.h"

namespace boost::asio {
class io_context;
}

namespace net {

class TransportFactoryImpl : public TransportFactory {
 public:
  explicit TransportFactoryImpl(boost::asio::io_context& io_context);

  // Returns nullptr if parameters are invalid.
  virtual std::unique_ptr<Transport> CreateTransport(
      const TransportString& transport_string,
      Logger* logger) override;

 private:
  boost::asio::io_context& io_context_;
};

}  // namespace net
