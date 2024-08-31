#pragma once

#include "transport/awaitable.h"
#include "transport/expected.h"

#include <span>

namespace transport {

class any_transport;

// Reads full data buffer.
[[nodiscard]] awaitable<expected<size_t>> Read(any_transport& transport,
                                               std::span<char> data);

}  // namespace transport
