#pragma once

#include "net/logger.h"

#include <iostream>
#include <mutex>

namespace net {

class DebugLogger : public Logger {
 public:
  virtual void Write(LogSeverity severity, const char* message) const override {
    std::lock_guard lock{mutex_};
    std::cout << message << "\n";
  }

  virtual void WriteV(LogSeverity severity,
                      const char* format,
                      va_list args) const PRINTF_FORMAT(3, 0) override {
    char message[1024];
    std::vsnprintf(message, sizeof(message), format, args);
    Write(severity, message);
  }

  virtual void WriteF(LogSeverity severity,
                      _Printf_format_string_ const char* format,
                      ...) const PRINTF_FORMAT(3, 4) override {
    va_list args;
    va_start(args, format);
    WriteV(severity, format, args);
    va_end(args);
  }

 private:
  mutable std::mutex mutex_;
};

}  // namespace net