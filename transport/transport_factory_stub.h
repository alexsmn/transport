#pragma once

#include "transport/any_transport.h"
#include "transport/transport_factory.h"
#include "transport/transport_string.h"

#include <string>
#include <unordered_map>

namespace transport {

// A TransportFactory that hands out transports registered in advance, keyed
// by the string form of the TransportString a caller will ask for. Each
// registered transport is handed out once; an unknown or already-taken name
// answers ERR_INVALID_ARGUMENT, as a real factory does for a string it cannot
// serve.
class StubTransportFactory : public TransportFactory {
 public:
  // Registers |transport| to be returned for |name|. A second registration
  // under the same name is ignored.
  void AddTransport(const std::string& name, any_transport transport);

  // TransportFactory
  virtual expected<any_transport> CreateTransport(
      const TransportString& transport_string,
      const executor& executor,
      const log_source& log = {}) override;

 private:
  std::unordered_map<std::string, any_transport> transports_;
};

inline void StubTransportFactory::AddTransport(const std::string& name,
                                               any_transport transport) {
  transports_.try_emplace(name, std::move(transport));
}

inline expected<any_transport> StubTransportFactory::CreateTransport(
    const TransportString& transport_string,
    const executor& /*executor*/,
    const log_source& /*log*/) {
  auto i = transports_.find(transport_string.ToString());
  if (i == transports_.end())
    return ERR_INVALID_ARGUMENT;

  any_transport transport = std::move(i->second);
  transports_.erase(i);
  return transport;
}

}  // namespace transport
