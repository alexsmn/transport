#include "net/socket_pool.h"

#include "net/base/winsock_init.h"
#include "net/socket.h"
#include "net/socket_delegate.h"
#include "net/socket_window.h"

namespace net {

SocketPool* SocketPool::instance_ = NULL;

SocketPool::SocketPool() : window_(new SocketWindow(*this)) {
  assert(!instance_);
  instance_ = this;
  EnsureWinsockInit();
}

SocketPool::~SocketPool() {
  assert(instance_ == this);
  assert(window_);

  // Note: SocketPool can be destroyed during processing of some socket callback method.
  // So, we need to delete window only on OnFinalMessage().
  window_->Destroy();
  window_ = NULL;

  instance_ = NULL;
}

SocketPool& SocketPool::get() {
  if (!instance_)
    instance_ = new SocketPool();
  return *instance_;
}

Error SocketPool::BeginSelect(Socket& socket) {
  assert(select_.find(socket.handle_) == select_.end());

  if (WSAAsyncSelect(socket.handle_, window_->m_hWnd,
                     SocketWindow::WM_SOCKET, FD_ALL_EVENTS) == SOCKET_ERROR)
    return MapSystemError(WSAGetLastError());

  select_[socket.handle_] = &socket;
  return OK;
}

void SocketPool::EndSelect(Socket& socket) {
  SelectionMap::iterator i = select_.find(socket.handle_);
  assert(i != select_.end());
  assert(i->second == &socket);
  select_.erase(i);

  WSAAsyncSelect(socket.handle_, window_->m_hWnd, 0, 0);

  // All currently pending messages for this socket shall be ignored until WM_RESUME.
  suspended_handles_.insert(socket.handle_);
  window_->PostMessage(SocketWindow::WM_RESUME, static_cast<WPARAM>(socket.handle_));
}

void SocketPool::ResumeSelect(SocketHandle handle) {
  std::multiset<SocketHandle>::iterator i = suspended_handles_.find(handle);
  assert(i != suspended_handles_.end());
  suspended_handles_.erase(i);
}

Error SocketPool::BeginResolve(Socket& socket, const char* host) {
  assert(resolve_.find(socket.resolve_) == resolve_.end());
  assert(!socket.resolve_);
  assert(socket.resolve_buffer_.empty());

  socket.resolve_buffer_.resize(MAXGETHOSTSTRUCT);

  socket.resolve_ = WSAAsyncGetHostByName(reinterpret_cast<SocketWindow*>(window_)->m_hWnd,
    SocketWindow::WM_RESOLVE, host, socket.resolve_buffer_.data(), MAXGETHOSTSTRUCT);
  if (!socket.resolve_) {
    Error error = MapSystemError(WSAGetLastError());
    socket.resolve_buffer_.clear();
    socket.resolve_buffer_.shrink_to_fit();
    return error;
  }

  resolve_[socket.resolve_] = &socket;
  return OK;
}

void SocketPool::EndResolve(Socket& socket) {
  assert(socket.resolve_);
  assert(!socket.resolve_buffer_.empty());

  ResolutionMap::iterator i = resolve_.find(socket.resolve_);
  assert(i != resolve_.end());
  assert(i->second == &socket);
  resolve_.erase(i);

  WSACancelAsyncRequest(socket.resolve_);

  socket.resolve_ = NULL;
  socket.resolve_buffer_.clear();
  socket.resolve_buffer_.shrink_to_fit();
}

void SocketPool::ProcessEvent(SocketHandle handle, unsigned event, int os_error) {
  SelectionMap::iterator i = select_.find(handle);
  if (i == select_.end())
    return;

  // Check socket messages are not suspened.
  if (suspended_handles_.find(handle) != suspended_handles_.end())
    return;

  Socket& socket = *i->second;
  Error error = os_error == 0 ? OK : MapSystemError(os_error);

  switch (event) {
    case FD_ACCEPT:
      socket.OnAccepted();
      break;
    case FD_CONNECT:
      socket.OnConnected(error);
      break;
    case FD_CLOSE:
      socket.OnClosed(error);
      break;
    case FD_READ:
      assert(!error);
      socket.OnDataReceived();
      break;
    case FD_WRITE:
      assert(!error);
      socket.OnSendPossible();
      break;
  }
}

void SocketPool::ProcessResolve(SocketResolveHandle resolve, int os_error) {
  ResolutionMap::iterator i = resolve_.find(resolve);
  if (i == resolve_.end())
    return;

  Error error = (os_error == 0) ? OK : MapSystemError(os_error);

  Socket& socket = *i->second;
  resolve_.erase(i);

  assert(socket.resolve_ == resolve);
  assert(socket.resolve_ == resolve);
  socket.OnResolved(error);
}

} // namespace net
