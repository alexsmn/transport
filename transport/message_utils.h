#pragma once

#include "transport/any_transport.h"
#include "transport/bytemsg.h"

#include <stdexcept>
#include <string>

namespace transport {

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
inline awaitable<error_code> ReadMessage(any_transport& transport,
                                    size_t max_size,
                                    std::vector<char>& buffer) {
  buffer.resize(max_size);
  NET_ASSIGN_OR_CO_RETURN(auto bytes_read, co_await transport.read(buffer));
  buffer.resize(bytes_read);
  co_return OK;
}

}  // namespace transport
