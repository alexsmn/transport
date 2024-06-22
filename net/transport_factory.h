#pragma once

#include "net/executor.h"
#include "net/transport.h"

#include <memory>

namespace net {

class Logger;
class Transport;
class TransportString;

class TransportFactory {
 public:
  virtual ~TransportFactory() {}

  // Returns nullptr if parameters are invalid.
  virtual std::unique_ptr<Transport> CreateTransport(
      const TransportString& transport_string,
      const net::Executor& executor,
      std::shared_ptr<const Logger> logger = nullptr) = 0;
};

}  // namespace net
