#include "net/pipe_transport.h"

#include "net/base/net_errors.h"

#include <boost/locale/encoding_utf.hpp>
#include <cassert>
#include <chrono>
#include <windows.h>

using namespace std::chrono_literals;

namespace net {

PipeTransport::PipeTransport(const Executor& executor) : timer_{executor} {}

PipeTransport::~PipeTransport() {
  if (handle_ != INVALID_HANDLE_VALUE)
    CloseHandle(handle_);
}

void PipeTransport::Init(const std::wstring& name, bool server) {
  name_ = name;
  server_ = server;
}

promise<void> PipeTransport::Open(const Handlers& handlers) {
  assert(handle_ == INVALID_HANDLE_VALUE);

  HANDLE handle;

  if (server_) {
    handle = CreateNamedPipeW(name_.c_str(), PIPE_ACCESS_DUPLEX,
                              PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_NOWAIT,
                              PIPE_UNLIMITED_INSTANCES, 1024, 1024, 0, nullptr);
  } else {
    handle = CreateFileW(name_.c_str(), GENERIC_READ | GENERIC_WRITE, 0,
                         nullptr, OPEN_EXISTING, 0, nullptr);
  }

  if (handle == INVALID_HANDLE_VALUE) {
    handlers.on_close(ERR_FAILED);
    return make_error_promise(ERR_FAILED);
  }

  if (server_) {
    if (!ConnectNamedPipe(handle, nullptr)) {
      DWORD error = GetLastError();
      if (error != ERROR_PIPE_LISTENING) {
        CloseHandle(handle);
        handlers.on_close(ERR_FAILED);
        return make_error_promise(ERR_FAILED);
      }
    }
  }

  handlers_ = handlers;
  handle_ = handle;
  connected_ = true;

  // TODO: Fix ASAP.
  timer_.StartRepeating(10ms, [this] { OnTimer(); });

  if (auto on_open = std::move(handlers_.on_open)) {
    on_open();
  }

  return make_resolved_promise();
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

boost::asio::awaitable<size_t> PipeTransport::Write(std::vector<char> data) {
  DWORD bytes_written = 0;
  if (!WriteFile(handle_, data.data(), data.size(), &bytes_written, nullptr)) {
    throw net_exception{ERR_FAILED};
  }

  co_return bytes_written;
}

void PipeTransport::OnTimer() {
  if (handlers_.on_data)
    handlers_.on_data();
}

std::string PipeTransport::GetName() const {
  return "PIPE " + boost::locale::conv::utf_to_utf<char>(name_);
}

}  // namespace net
