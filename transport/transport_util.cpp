#include "transport/transport_util.h"

#include "transport/any_transport.h"

namespace transport {

awaitable<expected<size_t>> Read(any_transport& transport,
                                 std::span<char> data) {
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

}  // namespace transport