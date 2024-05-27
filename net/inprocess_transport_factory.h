#pragma once

#include "net/inprocess_transport.h"
#include "net/transport_factory.h"
#include "net/transport_string.h"

#include <unordered_map>

namespace net {

class InprocessTransportFactory : public TransportFactory {
 public:
  // TransportFactory
  virtual std::unique_ptr<Transport> CreateTransport(
      const TransportString& transport_string,
      const net::Executor& executor,
      std::shared_ptr<const Logger> logger = nullptr) override;

 private:
  InprocessTransportHost inprocess_transport_host_;
};

inline std::unique_ptr<Transport> InprocessTransportFactory::CreateTransport(
    const TransportString& transport_string,
    const net::Executor& executor,
    std::shared_ptr<const Logger> logger) {
  auto channel_name = transport_string.GetParamStr(TransportString::kParamName);
  return transport_string.IsActive()
             ? inprocess_transport_host_.CreateClient(executor, channel_name)
             : inprocess_transport_host_.CreateServer(executor, channel_name);
}

}  // namespace net
