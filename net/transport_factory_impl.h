#pragma once

#include "net/transport_factory.h"

#include <boost/asio.hpp>

namespace net {

class TransportFactoryImpl : public TransportFactory {
 public:
  explicit TransportFactoryImpl(boost::asio::io_service& io_service);

  // Returns nullptr if parameters are invalid.
  virtual std::unique_ptr<Transport> CreateTransport(
      const TransportString& transport_string) override;

 private:
  boost::asio::io_service& io_service_;
};

}  // namespace net
