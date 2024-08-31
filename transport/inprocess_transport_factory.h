#pragma once

#include "transport/inprocess_transport.h"
#include "transport/transport_factory.h"
#include "transport/transport_string.h"

#include <unordered_map>

namespace transport {

class InprocessTransportFactory : public TransportFactory {
 public:
  // TransportFactory
  virtual expected<any_transport> CreateTransport(
      const TransportString& transport_string,
      const executor& executor,
      const log_source& log = {}) override;

 private:
  InprocessTransportHost inprocess_transport_host_;
};

inline expected<any_transport> InprocessTransportFactory::CreateTransport(
    const TransportString& transport_string,
    const executor& executor,
    const log_source& log) {
  auto channel_name = transport_string.GetParamStr(TransportString::kParamName);

  return transport_string.active()
             ? inprocess_transport_host_.CreateClient(executor, channel_name)
             : inprocess_transport_host_.CreateServer(executor, channel_name);
}

}  // namespace transport
