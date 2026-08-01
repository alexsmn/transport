#pragma once

#include "transport/bytemsg.h"

namespace transport {

template<size_t SIZE = 64>
class ByteBuffer : public ByteMessage {
 public:
  ByteBuffer()
      : ByteMessage(buffer_, sizeof(buffer_)) {
  }

 private:
  uint8_t buffer_[SIZE];
};

} // namespace transport
