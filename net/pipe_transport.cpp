#include "net/pipe_transport.h"

#include "base/strings/stringprintf.h"
#include "net/base/net_errors.h"

#include <cassert>
#include <windows.h>

using namespace std::chrono_literals;

namespace net {

PipeTransport::PipeTransport(boost::asio::io_service& io_service)
    : handle_(INVALID_HANDLE_VALUE),
      server_{false},
      timer_{io_service} {
}

PipeTransport::~PipeTransport() {
  if (handle_ != INVALID_HANDLE_VALUE)
    CloseHandle(handle_);
}

void PipeTransport::Init(const base::string16& name, bool server) {
  name_ = name;
  server_ = server;
}

Error PipeTransport::Open() {
  assert(handle_ == INVALID_HANDLE_VALUE);

  HANDLE handle;
  
  if (server_) {
    handle = CreateNamedPipe(name_.c_str(), PIPE_ACCESS_DUPLEX,
      PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_NOWAIT,
      PIPE_UNLIMITED_INSTANCES, 1024, 1024, 0, NULL);
  } else {
    handle = CreateFile(name_.c_str(), GENERIC_READ | GENERIC_WRITE,
      0, NULL, OPEN_EXISTING, 0, NULL);
  }
  if (handle == INVALID_HANDLE_VALUE)
    return ERR_FAILED;
    
  if (server_) {
    if (!ConnectNamedPipe(handle, NULL)) {
      DWORD error = GetLastError();
      if (error != ERROR_PIPE_LISTENING) {
        CloseHandle(handle);
        return ERR_FAILED;
      }
    }
  }
    
  handle_ = handle;
  connected_ = true;
  
  // TODO: Fix ASAP.
  timer_.StartRepeating(10ms, [this] { OnTimer(); });

  if (delegate_)
    delegate_->OnTransportOpened(); 
  
  return OK;
}

void PipeTransport::Close() {
  if (handle_ != INVALID_HANDLE_VALUE) {
    CloseHandle(handle_);
    handle_ = INVALID_HANDLE_VALUE;
  }
}

int PipeTransport::Read(void* data, size_t len) {
  OVERLAPPED overlapped = { 0 };
  DWORD bytes_read;
  if (!ReadFile(handle_, data, len, &bytes_read, &overlapped))
    return ERR_FAILED;
  return bytes_read;
}

int PipeTransport::Write(const void* data, size_t len) {
  DWORD bytes_written;
  if (!WriteFile(handle_, data, len, &bytes_written, NULL))
    return ERR_FAILED;
  if (bytes_written != len)
    return ERR_FAILED;
  return static_cast<int>(bytes_written);
}

void PipeTransport::OnTimer() {
  if (delegate_)
    delegate_->OnTransportDataReceived();
}

std::string PipeTransport::GetName() const {
  return base::StringPrintf("PIPE %s", name_.c_str());
}

} // namespace net
