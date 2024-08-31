#pragma once

#include "transport/any_transport.h"
#include "transport/executor.h"

#include <unordered_map>

namespace transport {

class InprocessTransportHost {
 public:
  any_transport CreateServer(const executor& executor,
                             std::string_view channel_name);

  any_transport CreateClient(const executor& executor,
                             std::string_view channel_name);

 private:
  class AcceptedClient;
  class Client;
  class Server;

  Server* FindServer(const std::string& channel_name);

  std::unordered_map<std::string /*channel_name*/, Server*> listeners_;
};

}  // namespace transport
