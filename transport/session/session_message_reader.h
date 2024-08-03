#pragma once

#include "transport/logger.h"
#include "transport/message_reader.h"

namespace transport {

class SessionMessageReader : public MessageReaderImpl<4096> {
 protected:
  virtual bool GetBytesExpected(const void* buf, size_t len,
                                size_t& expected) const {
    // check size is received
    if (len < 2) {
      expected = 2;
      return true;
    }

    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(buf);

    // check data is received
    size_t size = *reinterpret_cast<const uint16_t*>(bytes);
    if (size > 4096) {
      logger().Write(LogSeverity::Error, "Size is too long");
      return false;
    }
    
    expected = 2 + size;
    return true;
  }

  virtual MessageReader* Clone() {
    return new SessionMessageReader();
  }
};

} // namespace transport
