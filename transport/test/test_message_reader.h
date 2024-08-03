#pragma once

#include "transport/message_reader.h"

namespace transport {

// Use the first byte as the message size.
class TestMessageReader : public MessageReaderImpl<1024> {
 public:
  virtual MessageReader* Clone() override {
    return new TestMessageReader();
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

}  // namespace transport
