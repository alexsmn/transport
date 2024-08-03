#pragma once

#include "transport/logger.h"

#include <cstdarg>

namespace transport {

class log_source {
 public:
  log_source() = default;

  explicit log_source(std::shared_ptr<const Logger> logger)
      : logger_{std::move(logger)} {}

  void write(LogSeverity severity, const char* message) const {
    if (logger_) {
      logger_->Write(severity, message);
    }
  }

  void write_v(LogSeverity severity, const char* format, va_list args) const
      PRINTF_FORMAT(3, 0) {
    if (logger_) {
      logger_->WriteV(severity, format, args);
    }
  }

  void write_f(LogSeverity severity,
               _Printf_format_string_ const char* format,
               ...) const PRINTF_FORMAT(3, 4) {
    if (logger_) {
      va_list args;
      va_start(args, format);
      logger_->WriteV(severity, format, args);
      va_end(args);
    }
  }

 private:
  std::shared_ptr<const Logger> logger_;
};

}  // namespace transport