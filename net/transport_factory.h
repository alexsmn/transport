#pragma once

#include <memory>

namespace net {

class Transport;
class TransportString;

class TransportFactory {
 public:
  virtual ~TransportFactory() {}

  // Returns nullptr if parameters are invalid.
  virtual std::unique_ptr<Transport> CreateTransport(
      const TransportString& transport_string) = 0;
};

}  // namespace net
