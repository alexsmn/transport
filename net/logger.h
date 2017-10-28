#pragma once

#include <memory>

namespace net {

enum class LogSeverity {
  Normal = 0,
  Warning = 1,
  Error = 2,
  Fatal = 3,
  Count = 4
};

class Logger {
 public:
  virtual ~Logger() { }
 
  virtual void Write(LogSeverity severity, const char* message) const = 0;
  virtual void WriteV(LogSeverity severity, const char* format, va_list args) const = 0;
  virtual void WriteF(LogSeverity severity, const char* format, ...) const = 0;
};

class NullLogger : public Logger {
 public:
  virtual void Write(LogSeverity severity, const char* message) const final {}
  virtual void WriteV(LogSeverity severity, const char* format, va_list args) const final {}
  virtual void WriteF(LogSeverity severity, const char* format, ...) const final {}

  static std::shared_ptr<NullLogger> GetInstance() {
    static auto logger = std::make_shared<NullLogger>();
    return logger;
  }
};

} // namespace net