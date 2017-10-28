#pragma once

#include "base/memory/ref_counted.h"
#include "base/location.h"
#include "net/base/net_errors.h"
#include "net/socket_handle.h"

namespace net {

class SocketDelegate;
class SocketPool;

class Socket {
 public:
  Socket(const tracked_objects::Location& location,
         SocketDelegate* delegate);
  ~Socket();

  void set_delegate(SocketDelegate* delegate) { delegate_ = delegate; }

  bool is_closed() const { return state_ == CLOSED; }
  bool is_connected() const { return state_ == CONNECTED; }
  bool is_connecting() const { return state_ == RESOLVING || state_ == CONNECTING; }

  Error Connect(const char* host, unsigned short port);
  Error Listen(unsigned short port);

  bool GetLocalAddress(unsigned& ip, unsigned short& port) const;
  bool GetPeerAddress(unsigned& ip, unsigned short& port) const;

  int Read(void* data, size_t len);
  int Write(const void* data, size_t len);

  Error Shutdown();
  void Close();

private:
  friend class SocketPool;

  class Context : public base::RefCounted<Context> {
   public:
    Context() : destroyed_(false) {}
    
    bool is_destroyed() const { return destroyed_; }
    void Destroyed() { destroyed_ = true; }

   private:
    bool destroyed_;
  };

  enum State { CLOSED, IDLE, RESOLVING, CONNECTING, CONNECTED, LISTENING };

  // Set socket options.
  Error Configure(SocketHandle handle);
  // Attach accepted socket. Set socket to CONNECTED states.
  Error Attach(SocketHandle handle);
  // Create socket. |port| is local port and may be 0.
  Error Create(unsigned short port = 0);
  // Connect to specified IP.
  Error ConnectToIP(unsigned long ip, unsigned short port);

  void OnAccepted();
  void OnConnected(Error error);
  // Connection is closed by peer.
  void OnClosed(Error error);
  // Host name resolve completed.
  void OnResolved(Error error);
  void OnDataReceived();
  void OnSendPossible();

  void set_state(State state);
  static const char* FormatState(State state);

  scoped_refptr<SocketPool> pool_;
  State state_;
  SocketDelegate* delegate_;
  SocketHandle handle_;
  SocketResolveHandle resolve_;
  std::vector<char> resolve_buffer_;
  unsigned short port_;

  scoped_refptr<Context> context_;

  DISALLOW_COPY_AND_ASSIGN(Socket);
};

} // namespace net
