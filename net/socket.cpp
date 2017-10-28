#include "base/logging.h"
#include "net/socket.h"
#include "net/socket_delegate.h"
#include "net/socket_pool.h"

#include <winsock2.h>

#pragma comment(lib, "ws2_32.lib")

namespace net {

Socket::Socket(const tracked_objects::Location& location, SocketDelegate* delegate)
    : pool_(&SocketPool::get()),
      delegate_(delegate),
      state_(CLOSED),
      handle_(INVALID_SOCKET),
      resolve_(NULL) {
}

Socket::~Socket() {
  Close();
}

bool Socket::GetLocalAddress(unsigned& ip, unsigned short& port) const {
  assert(state_ != CLOSED);
  sockaddr_in addr;
  int len = sizeof(addr);
  if (getsockname(handle_,(sockaddr*)&addr, &len) == SOCKET_ERROR)
    return false;
    
  ip = *reinterpret_cast<unsigned long*>(&addr.sin_addr);
  port = ntohs(addr.sin_port);
  return true;
}

bool Socket::GetPeerAddress(unsigned& ip, unsigned short& port) const {
  assert(state_ != CLOSED);
  sockaddr_in addr;
  int len = sizeof(addr);
  if (getpeername(handle_,(sockaddr*)&addr, &len) == SOCKET_ERROR)
    return false;
    
  ip = *reinterpret_cast<unsigned long*>(&addr.sin_addr);
  port = ntohs(addr.sin_port);
  return true;
}

Error Socket::Create(unsigned short port) {
  assert(!context_);
  assert(state_ == CLOSED);
  assert(handle_ == INVALID_SOCKET);

  SOCKET socket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, 0);
  if (socket == INVALID_SOCKET) {
    Error error = MapSystemError(WSAGetLastError());
    assert(error != OK);
    LOG(WARNING) << "Socket creation error " << ErrorToString(error);
    return error;
  }

  LOG(INFO) << "Socket " << (unsigned)socket << " created";

  // Bind.
  sockaddr_in local;
  memset(&local, 0, sizeof(local));
  local.sin_family = PF_INET;
  local.sin_port = htons(port);
  if (bind(socket, (sockaddr*)&local, sizeof(local)) == SOCKET_ERROR) {
    Error error = MapSystemError(WSAGetLastError());
    closesocket(socket);
    LOG(WARNING) << "Socket " << (unsigned)socket
               << " bind error " << ErrorToString(error);
    return error;
  }

  Error error = Configure(socket);
  if (error != OK) {
    closesocket(socket);
    return error;
  }

  handle_ = static_cast<SocketHandle>(socket);

  error = pool_->BeginSelect(*this);
  if (error != OK) {
    closesocket(handle_);
    handle_ = INVALID_SOCKET;
    LOG(WARNING) << "Socket " << (unsigned)socket
               << " select error " << ErrorToString(error);
    return error;
  }

  set_state(IDLE);
  context_ = new Context;

  return OK;
}

Error Socket::Configure(SocketHandle handle) {
  // Set write buffer size.
  int send_buf = 1024 * 1024;
  setsockopt(handle, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<char*>(&send_buf),
      sizeof(send_buf));

  // Turn off send delay.
  BOOL delay = FALSE;
  if (setsockopt(handle, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<char*>(&delay),
                 sizeof(delay)) == SOCKET_ERROR) {
    Error error = MapSystemError(WSAGetLastError());
    LOG(WARNING) << "Socket " << (unsigned)handle
                 << " setsockopt(TCP_NODELAY) error " << ErrorToString(error);
  }

  // Allow to transmit sent data on close.
  linger l = { 1, 3 };
  if (setsockopt(handle, SOL_SOCKET, SO_LINGER, reinterpret_cast<char*>(&l),
                 sizeof(l)) == SOCKET_ERROR) {
    Error error = MapSystemError(WSAGetLastError());
    LOG(WARNING) << "Socket " << (unsigned)handle
                 << " setsockopt(SO_LINGER) error " << ErrorToString(error);
  }

  return OK;
}

Error Socket::Attach(SocketHandle handle) {
  assert(state_ == CLOSED);
  assert(handle_ == INVALID_SOCKET);
  assert(!context_);

  LOG(INFO) << "socket attached [handle=" << (unsigned)handle << "]";

  Error error = Configure(handle);
  if (error != OK)
    return error;

  handle_ = handle;

  error = pool_->BeginSelect(*this);
  if (error != OK) {
    handle_ = INVALID_SOCKET;
    LOG(WARNING) << "Socket " << (unsigned)socket
               << " select error " << ErrorToString(error);
    return error;
  }

  set_state(CONNECTED);
  context_ = new Context;

  return OK;
}

void Socket::Close() {
  if (state_ == CLOSED)
    return;

  if (state_ == RESOLVING)
    pool_->EndResolve(*this);

  pool_->EndSelect(*this);

  set_state(CLOSED);

  ::shutdown(handle_, SD_SEND);
  closesocket(handle_);

  LOG(INFO) << "Socket " << (unsigned)handle_ << " closed";

  handle_ = INVALID_SOCKET;

  if (context_) {
    context_->Destroyed();
    context_ = NULL;
  }

  assert(resolve_buffer_.empty());
}

int Socket::Read(void* data, size_t len) {
  assert(state_ == CONNECTED);

  int res = recv(handle_, reinterpret_cast<char*>(data), static_cast<int>(len), 0);
  if (res >= 0)
    return res;

  int os_error = WSAGetLastError();
  return (os_error == WSAEWOULDBLOCK) ? OK : MapSystemError(os_error);
}

int Socket::Write(const void* data, size_t len) {
  assert(state_ == CONNECTED);

  int res = send(handle_, reinterpret_cast<const char*>(data), static_cast<int>(len), 0);
  if (res >= 0)
    return res;

  int os_error = WSAGetLastError();
  return (os_error == WSAEWOULDBLOCK) ? OK : MapSystemError(os_error);
}

void Socket::OnDataReceived() {
  if (delegate_)
    delegate_->OnSocketDataReceived();
}

void Socket::OnSendPossible() {
  if (delegate_)
    delegate_->OnSocketSendPossible();
}

Error Socket::Listen(unsigned short port) {
  Error error = Create(port);
  if (error != OK)
    return error;

  if (::listen(handle_, SOMAXCONN) == SOCKET_ERROR) {
    Error error = MapSystemError(WSAGetLastError());
    assert(error == OK);
    LOG(WARNING) << "Socket " << (unsigned)socket
               << " listen error " << ErrorToString(error);
    return error;
  }

  LOG(INFO) << "Socket " << (unsigned)handle_
            << " is listening on port " << (unsigned)port;

  set_state(LISTENING);

  return OK;
}

Error Socket::Connect(const char* host, unsigned short port) {
  if (state_ == CLOSED) {
    Error error = Create();
    if (error != OK)
      return error;
  }

  assert(state_ == IDLE);

  unsigned long ip = inet_addr(host);

  // Check we don't need to resolve host name into IP address.
  if (ip != INADDR_NONE)
    return ConnectToIP(ip, port);

  Error error = pool_->BeginResolve(*this, host);
  if (error != OK)
    return error;

  LOG(INFO) << "Socket " << (unsigned)handle_
            << " is resolving host " << host;

  set_state(RESOLVING);

  // Save |port| for future use.
  port_ = port;

  return OK;
}

Error Socket::ConnectToIP(unsigned long ip, unsigned short port) {
  assert(state_ == IDLE || state_ == RESOLVING);

  set_state(CONNECTING);

  sockaddr_in remote;
  memset(&remote, 0, sizeof(remote));
  remote.sin_family = PF_INET;
  memcpy(&remote.sin_addr, &ip, 4);
  remote.sin_port = htons(port);

  const unsigned char* ips = (const unsigned char*)&ip;
  LOG(INFO) << "Socket " << (unsigned)handle_
            << " is connecting to "
            << (unsigned)ips[0] << "." << (unsigned)ips[1] << "."
            << (unsigned)ips[2] << "." << (unsigned)ips[3]
            << ":" << (unsigned)port;

  if (::connect(handle_, (sockaddr*)&remote, sizeof(remote)) == SOCKET_ERROR) {
    int os_error = WSAGetLastError();
    // Check connection is pending.
    if (os_error != WSAEWOULDBLOCK) {
      set_state(IDLE);
      Error error = MapSystemError(os_error);
      LOG(WARNING) << "Socket " << (unsigned)socket
                 << " connect error " << ErrorToString(error);
      return error;
    }
  }

  // Connection can be completed immediately but we are still need to wait
  // for FD_CONNECT.
  return OK;
}

void Socket::OnConnected(Error error) {
  assert(context_);
  assert(!context_->is_destroyed());
  assert(state_ == CONNECTING);

  if (error == OK) {
    LOG(INFO) << "Socket " << (unsigned)handle_ << " connected";
  } else {
    LOG(WARNING) << "Socket " << (unsigned)handle_
               << " connect error " << ErrorToString(error);
  }

  set_state((error == OK) ? CONNECTED : IDLE);

  if (delegate_)
    delegate_->OnSocketConnected(error);
}

void Socket::OnResolved(Error error) {
  assert(state_ == RESOLVING);
  assert(resolve_);
  assert(!resolve_buffer_.empty());

  auto buffer = std::move(resolve_buffer_);
  resolve_ = nullptr;

  if (error) {
    LOG(WARNING) << "Socket " << (unsigned)handle_
      << " resolution error " << ErrorToString(error);

    set_state(IDLE);

    if (delegate_)
      delegate_->OnSocketConnected(error);
    return;
  }

  LOG(INFO) << "Socket " << (unsigned)handle_ << " resolved";

  const hostent* ent = reinterpret_cast<const hostent*>(buffer.data());
  unsigned long ip = *reinterpret_cast<const unsigned long*>(ent->h_addr_list[0]);

  error = ConnectToIP(ip, port_);
  if (error) {
    set_state(IDLE);
    if (delegate_)
      delegate_->OnSocketConnected(error);
    return;
  }

  // Otherwise, connection is pending.
}

void Socket::OnAccepted() {
  assert(context_);
  assert(!context_->is_destroyed());

  SOCKET handle = accept(handle_, NULL, NULL);
  if (handle == INVALID_SOCKET)
    return;

  LOG(INFO) << "socket accepted [handle=" << (unsigned)handle_
            << " accepted=" << (unsigned)handle << "]";

  if (!delegate_) {
    closesocket(handle);
    return;
  }

  auto socket = std::make_unique<Socket>(FROM_HERE, nullptr);
  if (socket->Attach(static_cast<SocketHandle>(handle)) != 0) {
    closesocket(handle);
    return;
  }

  delegate_->OnSocketAccepted(std::move(socket));
}

void Socket::OnClosed(Error error) {
  assert(context_);
  assert(!context_->is_destroyed());

  scoped_refptr<Context> context = context_;

  if (delegate_)
    delegate_->OnSocketDataReceived();

  if (context->is_destroyed())
    return;

  Close();

  if (delegate_)
    delegate_->OnSocketClosed(error);
}

Error Socket::Shutdown() {
  assert(state_ == CONNECTED);
  
  Error error = MapSystemError(shutdown(handle_, SD_SEND));
  if (error != OK) {
    LOG(WARNING) << "Socket " << (unsigned)handle_
               << " shutdown error " << ErrorToString(error);
    return error;
  }

  LOG(INFO) << "Socket " << (unsigned)handle_ << " is shutting down";
  return OK;
}

void Socket::set_state(State state) {
  if (state_ == state)
    return;

  LOG(INFO) << "Socket " << (unsigned)handle_
            << " state changed from " << FormatState(state_)
            << " to " << FormatState(state);

  state_ = state;
}

const char* Socket::FormatState(State state) {
  static const char* strs[] = { "CLOSED", "IDLE", "RESOLVING", "CONNECTING",
                                "CONNECTED", "LISTENING" };
  assert(static_cast<int>(state) < _countof(strs));
  return strs[static_cast<int>(state)];
}

} // namespace net
