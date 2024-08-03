#pragma once

#include "transport/awaitable.h"
#include "transport/error_or.h"

#include <span>

namespace transport {

class any_transport;
class Transport;

// Reads full data buffer.
[[nodiscard]] awaitable<ErrorOr<size_t>> Read(Transport& transport,
                                              std::span<char> data);

// Reads full data buffer.
[[nodiscard]] awaitable<ErrorOr<size_t>> Read(any_transport& transport,
                                              std::span<char> data);

}  // namespace transport
