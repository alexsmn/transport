#pragma once

#include "net/transport_factory.h"

#include <gmock/gmock.h>

namespace net {

class MockTransportFactory : public TransportFactory {
 public:
  MOCK_METHOD(ErrorOr<any_transport>,
              CreateTransport,
              (const TransportString& transport_string,
               const net::Executor& executor,
               std::shared_ptr<const Logger> logger));
};

}  // namespace net
