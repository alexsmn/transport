#pragma once

#include "net/bytemsg.h"

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

} // namespace net
