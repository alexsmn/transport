#pragma once

#include "transport/transport_factory.h"

#include <gmock/gmock.h>

namespace transport {

class MockTransportFactory : public TransportFactory {
 public:
  MOCK_METHOD(expected<any_transport>,
              CreateTransport,
              (const TransportString& transport_string,
               const Executor& executor,
               const log_source& log));
};

}  // namespace transport
