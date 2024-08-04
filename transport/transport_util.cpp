#include "transport/transport_util.h"

#include "transport/any_transport.h"
#include "transport/transport.h"

namespace transport {

awaitable<ErrorOr<size_t>> Read(Transport& transport, std::span<char> data) {
  assert(!transport.message_oriented());

  size_t bytes_read = 0;
  while (bytes_read < data.size()) {
    auto read_result = co_await transport.read(data.subspan(bytes_read));

    if (!read_result.ok() || *read_result == 0) {
      co_return read_result;
    }

    bytes_read += *read_result;
  }

  co_return bytes_read;
}

awaitable<ErrorOr<size_t>> Read(any_transport& transport,
                                std::span<char> data) {
  auto* impl = transport.get_impl();
  if (!impl) {
    co_return ERR_INVALID_HANDLE;
  }

  co_return co_await Read(*impl, data);
}

}  // namespace transport