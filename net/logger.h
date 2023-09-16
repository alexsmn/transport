#pragma once

#include "net/base/compiler_specific.h"

#include <cstdarg>
#include <memory>
#include <string>

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
  virtual ~Logger() {}

  virtual void Write(LogSeverity severity, const char* message) const = 0;
  virtual void WriteV(LogSeverity severity,
                      const char* format,
                      va_list args) const PRINTF_FORMAT(3, 0) = 0;
  virtual void WriteF(LogSeverity severity,
                      _Printf_format_string_ const char* format,
                      ...) const PRINTF_FORMAT(3, 4) = 0;
};

class NullLogger final : public Logger {
 public:
  virtual void Write(LogSeverity severity, const char* message) const override {
  }
  virtual void WriteV(LogSeverity severity,
                      const char* format,
                      va_list args) const override PRINTF_FORMAT(3, 0) {}
  virtual void WriteF(LogSeverity severity,
                      _Printf_format_string_ const char* format,
                      ...) const override PRINTF_FORMAT(3, 4) {}

  static std::shared_ptr<NullLogger> GetInstance() {
    static auto logger = std::make_shared<NullLogger>();
    return logger;
  }
};

class ProxyLogger final : public Logger {
 public:
  explicit ProxyLogger(std::shared_ptr<const Logger> underlying_logger,
                       const char* channel = nullptr)
      : underlying_logger_{underlying_logger}, prefix_{MakePrefix(channel)} {}

  virtual void Write(LogSeverity severity, const char* message) const override {
    if (!underlying_logger_)
      return;

    underlying_logger_->Write(severity, GetPrefixedMessage(message).c_str());
  }

  virtual void WriteV(LogSeverity severity,
                      const char* format,
                      va_list args) const override PRINTF_FORMAT(3, 0) {
    if (!underlying_logger_)
      return;

    underlying_logger_->WriteV(severity, GetPrefixedMessage(format).c_str(),
                               args);
  }

  virtual void WriteF(LogSeverity severity,
                      _Printf_format_string_ const char* format,
                      ...) const override PRINTF_FORMAT(3, 4) {
    if (!underlying_logger_)
      return;

    va_list args;
    va_start(args, format);
    underlying_logger_->WriteV(severity, GetPrefixedMessage(format).c_str(),
                               args);
    va_end(args);
  }

 private:
  static std::string MakePrefix(const char* channel) {
    std::string prefix;
    if (channel && channel[0]) {
      prefix += channel;
      prefix += ": ";
    }
    return prefix;
  }

  std::string GetPrefixedMessage(const char* message) const {
    return prefix_ + message;
  }

  const std::shared_ptr<const Logger> underlying_logger_;
  std::string prefix_;
};

}  // namespace net
