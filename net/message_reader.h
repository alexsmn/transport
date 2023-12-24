#pragma once

#include "net/bytemsg.h"
#include "net/logger.h"

#include <cassert>

namespace net {

// Usage pattern:
//    size_t my_bytes_expected(const void* buf, size_t len) { ... }
//
//    MessageReader<100, my_bytes_expected> my_reader;
//    size_t bytes_to_read;
//    while (bytes_to_read = my_reader.GetBytesToRead()) {
//       int res = my_stream.read(my_reader.ptr(), bytes_to_read);
//       if (res <= 0)
//          return;
//       my_reader.BytesRead(static_cast<size_t>(res));
//    }
//
//    if (my_reader.complete()) {
//       ByteMessage& msg = my_reader.message();
//       ...
//       my_reader.Reset();
//    }
//
// Don't forget to reset buffer on read error.

class MessageReader {
 public:
  MessageReader(void* buffer, size_t capacity)
      : buffer_(buffer, capacity),
        complete_(false),
        logger_(NullLogger::GetInstance()),
        error_correction_(false) {}
  virtual ~MessageReader() {}

  MessageReader(const MessageReader&) = delete;
  MessageReader& operator=(const MessageReader&) = delete;

  // Did message completely read.
  bool complete() const { return complete_; }
  // Get current message.
  const ByteMessage& message() const { return buffer_; }
  // Get read position.
  void* ptr() { return buffer_.ptr(); }

  bool has_error_correction() const { return error_correction_; }
  void set_error_correction(bool correction) { error_correction_ = correction; }

  void SetLogger(std::shared_ptr<const Logger> logger) {
    logger_ = std::move(logger);
  }

  bool TryCorrectError() { return error_correction_ && SkipFirstByte(); }

  // Number of bytes to pass for next read operation.
  bool GetBytesToRead(size_t& bytes_to_read) const {
    size_t expected = 0;
    if (!GetBytesExpected(buffer_.data, buffer_.size, expected))
      return false;
    assert(expected > 0);
    assert(expected <= buffer_.capacity);
    assert(buffer_.size <= expected);
    bytes_to_read = expected - buffer_.size;
    complete_ = bytes_to_read == 0;
    return true;
  }

  // Skip read bytes.
  void BytesRead(size_t count) { buffer_.Write(nullptr, count); }

  // Reset buffer.
  void Reset() {
    buffer_.Clear();
    complete_ = false;
  }

  bool SkipFirstByte() {
    if (buffer_.size == 0)
      return false;
    if (buffer_.size >= 2)
      memmove(buffer_.data, buffer_.data + 1, buffer_.size - 1);
    buffer_.size--;
    buffer_.pos = 0;
    return true;
  }

  [[nodiscard]] virtual MessageReader* Clone() = 0;

 protected:
  // Calculate expected message size. Shall return in |expected| number of
  // bytes required to complete message, or 0 if message is complete.
  // Shall return false on error.
  virtual bool GetBytesExpected(const void* buf,
                                size_t len,
                                size_t& expected) const = 0;

  const Logger& logger() const {
    assert(logger_);
    return *logger_;
  }

 private:
  ByteMessage buffer_;
  mutable bool complete_;
  std::shared_ptr<const Logger> logger_;
  bool error_correction_;
};

template <size_t MAX_SIZE>
class MessageReaderImpl : public MessageReader {
 public:
  MessageReaderImpl() : MessageReader(data_, sizeof(data_)) {}

 private:
  char data_[MAX_SIZE];
};

}  // namespace net