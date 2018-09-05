#pragma once

#include "net/base/net_export.h"
#include "net/transport.h"

#include <memory>

namespace net {

class Logger;
class Transport;
class TransportString;

class NET_EXPORT TransportFactory {
 public:
  virtual ~TransportFactory() {}

  // Returns nullptr if parameters are invalid.
  virtual std::unique_ptr<Transport> CreateTransport(
      const TransportString& transport_string,
      Logger* logger = nullptr) = 0;
};

}  // namespace net
