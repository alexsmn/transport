#pragma once

#include "net/transport.h"

#include <unordered_map>

namespace net {

class InprocessTransportHost {
 public:
  std::unique_ptr<Transport> CreateServer(std::string_view channel_name);
  std::unique_ptr<Transport> CreateClient(std::string_view channel_name);

 private:
  class AcceptedClient;
  class Client;
  class Server;

  Server* FindServer(const std::string& channel_name);

  std::unordered_map<std::string /*channel_name*/, Server*> listeners_;
};

}  // namespace net
