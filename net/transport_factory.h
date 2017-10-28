#pragma once

#include <boost/asio.hpp>
#include <memory>

namespace net {

class Transport;
class TransportString;

class TransportFactory {
 public:
  virtual ~TransportFactory() {}

  // Returns nullptr if parameters are invalid.
  virtual std::unique_ptr<Transport> CreateTransport(const TransportString& transport_string) = 0;
};

class TransportFactoryImpl : public TransportFactory {
 public:
  explicit TransportFactoryImpl(boost::asio::io_service& io_service);

  // Returns nullptr if parameters are invalid.
  virtual std::unique_ptr<Transport> CreateTransport(const TransportString& transport_string) override;

 private:
  boost::asio::io_service& io_service_;
};

} // namespace net
