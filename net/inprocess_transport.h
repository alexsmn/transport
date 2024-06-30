#pragma once

#include "net/any_transport.h"
#include "net/executor.h"

#include <unordered_map>

namespace net {

class InprocessTransportHost {
 public:
  any_transport CreateServer(const Executor& executor,
                             std::string_view channel_name);

  any_transport CreateClient(const Executor& executor,
                             std::string_view channel_name);

 private:
  class AcceptedClient;
  class Client;
  class Server;

  Server* FindServer(const std::string& channel_name);

  std::unordered_map<std::string /*channel_name*/, Server*> listeners_;
};

}  // namespace net
