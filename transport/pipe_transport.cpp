#include "transport/pipe_transport.h"

#include "transport/any_transport.h"
#include "transport/error.h"

#include <Windows.h>
#include <boost/locale/encoding_utf.hpp>
#include <cassert>

using namespace std::chrono_literals;

namespace transport {

PipeTransport::PipeTransport(const executor& executor) : executor_{executor} {}

PipeTransport::~PipeTransport() {
  if (handle_ != INVALID_HANDLE_VALUE) {
    CloseHandle(handle_);
  }
}

void PipeTransport::Init(const std::wstring& name, bool server) {
  name_ = name;
  server_ = server;
}

awaitable<error_code> PipeTransport::open() {
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
    co_return ERR_FAILED;
  }

  if (server_) {
    if (!ConnectNamedPipe(handle, nullptr)) {
      DWORD error = GetLastError();
      if (error != ERROR_PIPE_LISTENING) {
        CloseHandle(handle);
        co_return ERR_FAILED;
      }
    }
  }

  handle_ = handle;
  connected_ = true;

  co_return OK;
}

awaitable<error_code> PipeTransport::close() {
  if (handle_ == INVALID_HANDLE_VALUE) {
    co_return ERR_INVALID_HANDLE;
  }

  CloseHandle(handle_);
  handle_ = INVALID_HANDLE_VALUE;

  co_return OK;
}

awaitable<expected<any_transport>> PipeTransport::accept() {
  co_return ERR_ACCESS_DENIED;
}

awaitable<expected<size_t>> PipeTransport::read(std::span<char> data) {
  OVERLAPPED overlapped = {0};
  DWORD bytes_read;
  if (!ReadFile(handle_, data.data(), data.size(), &bytes_read, &overlapped)) {
    co_return ERR_FAILED;
  }

  co_return bytes_read;
}

awaitable<expected<size_t>> PipeTransport::write(std::span<const char> data) {
  DWORD bytes_written = 0;
  if (!WriteFile(handle_, data.data(), data.size(), &bytes_written, nullptr)) {
    co_return ERR_FAILED;
  }

  co_return bytes_written;
}

std::string PipeTransport::name() const {
  return "PIPE " + boost::locale::conv::utf_to_utf<char>(name_);
}

}  // namespace transport
