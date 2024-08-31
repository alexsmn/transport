#pragma once

#include "transport/any_transport.h"
#include "transport/executor.h"
#include "transport/expected.h"
#include "transport/log.h"

#include <memory>

namespace transport {

class TransportString;

class TransportFactory {
 public:
  virtual ~TransportFactory() {}

  // Returns nullptr if parameters are invalid.
  virtual expected<any_transport> CreateTransport(
      const TransportString& transport_string,
      const Executor& executor,
      const log_source& log = {}) = 0;
};

}  // namespace transport
