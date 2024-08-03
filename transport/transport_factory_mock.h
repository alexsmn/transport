#pragma once

#include "transport/transport_factory.h"

#include <gmock/gmock.h>

namespace transport {

class MockTransportFactory : public TransportFactory {
 public:
  MOCK_METHOD(ErrorOr<any_transport>,
              CreateTransport,
              (const TransportString& transport_string,
               const Executor& executor,
               std::shared_ptr<const Logger> logger));
};

}  // namespace transport
