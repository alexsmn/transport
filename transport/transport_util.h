#pragma once

#include "transport/awaitable.h"
#include "transport/error_or.h"

#include <span>

namespace transport {

class Transport;

// Reads full data buffer.
awaitable<ErrorOr<size_t>> Read(Transport& transport, std::span<char> data);

}  // namespace transport
