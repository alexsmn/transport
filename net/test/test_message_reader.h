#pragma once

#include "net/message_reader.h"

namespace net {

// Use the first byte as the message size.
class TestMessageReader : public MessageReaderImpl<1024> {
 public:
  virtual MessageReader* Clone() override {
    assert(false);
    return nullptr;
  }

 protected:
  virtual bool GetBytesExpected(const void* buf,
                                size_t len,
                                size_t& expected) const override {
    if (len < 1) {
      expected = 1;
      return true;
    }

    const char* bytes = static_cast<const char*>(buf);
    expected = 1 + static_cast<size_t>(bytes[0]);
    return true;
  }
};

}  // namespace net
