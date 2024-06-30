#pragma once

#include <string>

namespace net {

class CreateSessionInfo {
 public:
  CreateSessionInfo() {}
  
  CreateSessionInfo(const std::string& name, const std::string& password,
                    bool force)
      : name(name),
        password(password),
        force(force) {
  }  
 
  std::string name;
  std::string password;
  bool force = false;
};

class SessionInfo {
 public:
  unsigned user_id = 0;
  unsigned user_rights = 0;
};

typedef std::string SessionID;

SessionID CreateSessionID();

} // namespace net
