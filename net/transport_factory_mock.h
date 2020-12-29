#pragma once

#include "net/transport_factory.h"

#include <gmock/gmock.h>

namespace net {

class MockTransportFactory : public TransportFactory {
 public:
  MOCK_METHOD(std::unique_ptr<Transport>,
              CreateTransport,
              (const TransportString& transport_string,
               std::shared_ptr<const Logger> logger));
};

}  // namespace net
