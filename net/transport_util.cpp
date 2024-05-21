#include "net/transport_util.h"

#include "net/transport.h"

namespace net {

awaitable<ErrorOr<size_t>> Read(Transport& transport, std::span<char> data) {
  assert(!transport.IsMessageOriented());

  size_t bytes_read = 0;
  while (bytes_read < data.size()) {
    auto read_result = co_await transport.Read(data.subspan(bytes_read));

    if (!read_result.ok() || *read_result == 0) {
      co_return read_result;
    }

    bytes_read += *read_result;
  }

  co_return bytes_read;
}

}  // namespace net