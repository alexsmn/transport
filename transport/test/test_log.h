#pragma once

#include "transport/log.h"

#include <iostream>
#include <mutex>

namespace transport {

class TestLogSink : public LogSink {
 public:
  void Write(LogSeverity severity, std::string_view message) const override {
    std::lock_guard lock{mutex_};
    std::cout << message << "\n";
  }

 private:
  mutable std::mutex mutex_;
};

}  // namespace transport
