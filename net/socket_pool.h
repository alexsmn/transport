#pragma once

#include "base/memory/ref_counted.h"
#include "net/base/net_errors.h"
#include "net/socket_handle.h"

#include <set>
#include <map>

namespace net {

class Socket;
class SocketWindow;

class SocketPool : public base::RefCounted<SocketPool> {
 public:
  SocketPool();
  ~SocketPool();

  static SocketPool& get();

private:
  friend class SocketWindow;
  friend class Socket;

  typedef std::map<SocketHandle, Socket*> SelectionMap;
  typedef std::map<SocketResolveHandle, Socket*> ResolutionMap;

  Error BeginSelect(Socket& socket);
  void EndSelect(Socket& socket);
  void ResumeSelect(SocketHandle handle);

  Error BeginResolve(Socket& socket, const char* host);
  void EndResolve(Socket& socket);

  void ProcessEvent(SocketHandle handle, unsigned event, int os_error);
  void ProcessResolve(SocketResolveHandle resolve, int os_error);

  ResolutionMap resolve_;
  SelectionMap select_;
  SocketWindow* window_;
  static SocketPool* instance_;

  // Socket after close posts WM_RESUME into message queue and its handle is
  // stored in |suspened_handles_| set. All messages to these sockets shall be ignored
  // until WM_RESUME will be received.
  // Note there could be several WM_RESUME for same socket handle in queue.
  std::multiset<SocketHandle>	suspended_handles_;
};

} // namespace net
