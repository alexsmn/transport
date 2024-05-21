#pragma once

#include "net/awaitable.h"
#include "net/error_or.h"

#include <span>

namespace net {

class Transport;

// Reads full data buffer.
awaitable<ErrorOr<size_t>> Read(Transport& transport, std::span<char> data);

}  // namespace net
