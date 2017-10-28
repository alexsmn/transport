#pragma once

#include "net/socket_handle.h"

#include <atlbase.h>
#include <atlwin.h>

namespace net {

class SocketWindow : public CWindowImpl<SocketWindow> {
 public:
  enum { WM_SOCKET = WM_APP + 1, WM_RESOLVE, WM_RESUME };

  SocketWindow(SocketPool& pool)
      : pool_(&pool) {
    Create(HWND_MESSAGE);
    assert(m_hWnd);
  }

  ~SocketWindow() {
    assert(!m_hWnd);
  }

  void Destroy() {
    pool_ = NULL;
    if (m_hWnd)
      DestroyWindow();
    else
      delete this;
  }

  BEGIN_MSG_MAP(SocketWindow)
    MESSAGE_HANDLER(WM_SOCKET, OnEvent)
    MESSAGE_HANDLER(WM_RESOLVE, OnResolve)
    MESSAGE_HANDLER(WM_RESUME, OnResume)
  END_MSG_MAP()

  LRESULT OnEvent(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& /*bHandled*/) {
    SocketHandle handle = static_cast<SocketHandle>(wParam);
    unsigned event = WSAGETSELECTEVENT(lParam);
    int error = WSAGETSELECTERROR(lParam);
    if (pool_)
      pool_->ProcessEvent(handle, event, error);
    return 0;
  }

  LRESULT OnResolve(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& /*bHandled*/) {
    SocketResolveHandle resolve = reinterpret_cast<SocketResolveHandle>(wParam);
    int error = WSAGETSELECTERROR(lParam);
    if (pool_)
      pool_->ProcessResolve(resolve, error);
    return 0;
  }

  LRESULT OnResume(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& /*bHandled*/) {
    SocketHandle handle = static_cast<SocketHandle>(wParam);
    if (pool_)
      pool_->ResumeSelect(handle);
    return 0;
  }

  virtual void OnFinalMessage(HWND) {
    delete this;
  }

 private:
  SocketPool* pool_;

  DISALLOW_COPY_AND_ASSIGN(SocketWindow);
};

} // namespace net
