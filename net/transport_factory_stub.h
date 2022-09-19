#pragma once

#include "net/transport_factory.h"
#include "net/transport_string.h"

#include <unordered_map>

namespace net {

class StubTransportFactory : public TransportFactory {
 public:
  void AddTransport(const std::string& name,
                    std::unique_ptr<Transport> transport);

  // TransportFactory
  virtual std::unique_ptr<Transport> CreateTransport(
      const TransportString& transport_string,
      std::shared_ptr<const Logger> logger = nullptr) override;

 private:
  std::unordered_map<std::string, std::unique_ptr<Transport>> transports_;
};

inline void StubTransportFactory::AddTransport(
    const std::string& name,
    std::unique_ptr<Transport> transport) {
  transports_.try_emplace(name, std::move(transport));
}

inline std::unique_ptr<Transport> StubTransportFactory::CreateTransport(
    const TransportString& transport_string,
    std::shared_ptr<const Logger> logger) {
  auto i = transports_.find(transport_string.ToString());
  if (i == transports_.end())
    return nullptr;

  auto transport = std::move(i->second);
  transports_.erase(i);
  return transport;
}

}  // namespace net
