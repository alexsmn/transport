#pragma once

#include "net/any_transport.h"
#include "net/bytemsg.h"
#include "net/transport.h"

#include <stdexcept>
#include <string>

namespace net {

inline void WriteMessageString(ByteMessage& message, const std::string& str) {
  if (str.length() > 255)
    throw std::runtime_error("Too long string");
  message.WriteByte(static_cast<uint8_t>(str.length()));
  message.Write(str.data(), str.length());
}

inline std::string ReadMessageString(ByteMessage& message) {
  size_t length = message.ReadByte();
  const char* data = static_cast<const char*>(message.ptr());
  message.Read(NULL, length);
  return std::string(data, length);
}

inline unsigned short SwapBytesInWord(unsigned short value) {
  return ((value & 0x00FF) << 8) | ((value & 0xFF00) >> 8);
}

// Empty result buffer means connection is closed.
inline awaitable<Error> ReadMessage(Transport& transport,
                                    size_t max_size,
                                    std::vector<char>& buffer) {
  buffer.resize(max_size);
  NET_ASSIGN_OR_CO_RETURN(auto bytes_read, co_await transport.Read(buffer));
  buffer.resize(bytes_read);
  co_return OK;
}

inline awaitable<Error> ReadMessage(any_transport& transport,
                                    size_t max_size,
                                    std::vector<char>& buffer) {
  auto* impl = transport.get_impl();
  if (!impl) {
    co_return ERR_INVALID_HANDLE;
  }

  co_return co_await ReadMessage(*impl, max_size, buffer);
}

}  // namespace net
