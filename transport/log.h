#pragma once

#include "transport/compiler_specific.h"

#include <cstdarg>
#include <memory>
#include <string>

namespace transport {

enum class LogSeverity {
  Normal = 0,
  Warning = 1,
  Error = 2,
  Fatal = 3,
  Count = 4
};

// Underlying logging interface.
class LogSink {
 public:
  virtual ~LogSink() {}

  virtual void Write(LogSeverity severity, const char* message) const = 0;
  virtual void WriteV(LogSeverity severity,
                      const char* format,
                      va_list args) const PRINTF_FORMAT(3, 0) = 0;
  virtual void WriteF(LogSeverity severity,
                      _Printf_format_string_ const char* format,
                      ...) const PRINTF_FORMAT(3, 4) = 0;
};

class ProxyLogSink final : public LogSink {
 public:
  explicit ProxyLogSink(std::shared_ptr<const LogSink> underlying_sink,
                        std::string_view channel = {})
      : underlying_sink_{underlying_sink}, prefix_{MakePrefix(channel)} {}

  virtual void Write(LogSeverity severity, const char* message) const override {
    if (!underlying_sink_)
      return;

    underlying_sink_->Write(severity, GetPrefixedMessage(message).c_str());
  }

  virtual void WriteV(LogSeverity severity,
                      const char* format,
                      va_list args) const override PRINTF_FORMAT(3, 0) {
    if (!underlying_sink_)
      return;

    underlying_sink_->WriteV(severity, GetPrefixedMessage(format).c_str(),
                             args);
  }

  virtual void WriteF(LogSeverity severity,
                      _Printf_format_string_ const char* format,
                      ...) const override PRINTF_FORMAT(3, 4) {
    if (!underlying_sink_)
      return;

    va_list args;
    va_start(args, format);
    underlying_sink_->WriteV(severity, GetPrefixedMessage(format).c_str(),
                             args);
    va_end(args);
  }

 private:
  static std::string MakePrefix(std::string_view channel) {
    std::string prefix;
    if (!channel.empty()) {
      prefix += channel;
      prefix += ": ";
    }
    return prefix;
  }

  std::string GetPrefixedMessage(const char* message) const {
    return prefix_ + message;
  }

  const std::shared_ptr<const LogSink> underlying_sink_;
  std::string prefix_;
};

class log_source {
 public:
  log_source() = default;

  // Intentionally accept implicit conversion from the shared interface. It's
  // safe and also makes the interface backward compatible.
  log_source(std::shared_ptr<const LogSink> sink) : logger_{std::move(sink)} {}

  void write(LogSeverity severity, const char* message) const {
    if (!logger_) {
      return;
    }

    logger_->Write(severity, message);
  }

  void writev(LogSeverity severity, const char* format, va_list args) const
      PRINTF_FORMAT(3, 0) {
    if (!logger_) {
      return;
    }

    logger_->WriteV(severity, format, args);
  }

  void writef(LogSeverity severity,
              _Printf_format_string_ const char* format,
              ...) const PRINTF_FORMAT(3, 4) {
    if (!logger_) {
      return;
    }

    va_list args;
    va_start(args, format);
    logger_->WriteV(severity, format, args);
    va_end(args);
  }

  log_source with_channel(std::string_view channel) const {
    if (!logger_) {
      return {};
    }
    return log_source{std::make_shared<ProxyLogSink>(logger_, channel)};
  }

 private:
  std::shared_ptr<const LogSink> logger_;
};

}  // namespace transport
