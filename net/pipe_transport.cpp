#include "net/pipe_transport.h"

#include "net/base/net_errors.h"

#include <boost/locale/encoding_utf.hpp>
#include <cassert>
#include <windows.h>

using namespace std::chrono_literals;

namespace net {

PipeTransport::PipeTransport(boost::asio::io_service& io_service)
    : timer_{io_service} {}

PipeTransport::~PipeTransport() {
  if (handle_ != INVALID_HANDLE_VALUE)
    CloseHandle(handle_);
}

void PipeTransport::Init(const std::wstring& name, bool server) {
  name_ = name;
  server_ = server;
}

Error PipeTransport::Open(const Handlers& handlers) {
  assert(handle_ == INVALID_HANDLE_VALUE);

  handlers_ = handlers;

  HANDLE handle;

  if (server_) {
    handle = CreateNamedPipeW(name_.c_str(), PIPE_ACCESS_DUPLEX,
                              PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_NOWAIT,
                              PIPE_UNLIMITED_INSTANCES, 1024, 1024, 0, NULL);
  } else {
    handle = CreateFileW(name_.c_str(), GENERIC_READ | GENERIC_WRITE, 0, NULL,
                         OPEN_EXISTING, 0, NULL);
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

  if (handlers_.on_open)
    handlers_.on_open();

  return OK;
}

void PipeTransport::Close() {
  if (handle_ != INVALID_HANDLE_VALUE) {
    CloseHandle(handle_);
    handle_ = INVALID_HANDLE_VALUE;
  }

  handlers_ = {};
}

int PipeTransport::Read(std::span<char> data) {
  OVERLAPPED overlapped = {0};
  DWORD bytes_read;
  if (!ReadFile(handle_, data.data(), data.size(), &bytes_read, &overlapped))
    return ERR_FAILED;
  return bytes_read;
}

int PipeTransport::Write(std::span<const char> data) {
  DWORD bytes_written;
  if (!WriteFile(handle_, data.data(), data.size(), &bytes_written, NULL))
    return ERR_FAILED;
  if (bytes_written != data.size())
    return ERR_FAILED;
  return static_cast<int>(bytes_written);
}

void PipeTransport::OnTimer() {
  if (handlers_.on_data)
    handlers_.on_data();
}

std::string PipeTransport::GetName() const {
  return "PIPE " + boost::locale::conv::utf_to_utf<char>(name_);
}

}  // namespace net
