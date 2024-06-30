#pragma once

#include "net/any_transport.h"
#include "net/error_or.h"
#include "net/executor.h"

#include <memory>

namespace net {

class Logger;
class Transport;
class TransportString;

class TransportFactory {
 public:
  virtual ~TransportFactory() {}

  // Returns nullptr if parameters are invalid.
  virtual ErrorOr<any_transport> CreateTransport(
      const TransportString& transport_string,
      const net::Executor& executor,
      std::shared_ptr<const Logger> logger = nullptr) = 0;
};

}  // namespace net
