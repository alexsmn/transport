#pragma once

#include <format>
#include <memory>
#include <string>
#include <string_view>

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
  virtual ~LogSink() = default;
  LogSink() = default;
  LogSink(const LogSink&) = delete;
  LogSink& operator=(const LogSink&) = delete;
  LogSink(LogSink&&) = delete;
  LogSink& operator=(LogSink&&) = delete;

  virtual void Write(LogSeverity severity, std::string_view message) const = 0;
};

class ProxyLogSink final : public LogSink {
 public:
  explicit ProxyLogSink(std::shared_ptr<const LogSink> underlying_sink,
                        std::string_view channel = {})
      : underlying_sink_{std::move(underlying_sink)},
        prefix_{MakePrefix(channel)} {}

  void Write(LogSeverity severity, std::string_view message) const override {
    if (!underlying_sink_)
      return;

    underlying_sink_->Write(severity, GetPrefixedMessage(message));
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

  std::string GetPrefixedMessage(std::string_view message) const {
    return prefix_ + std::string(message);
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

  template <typename... Args>
  void write(LogSeverity severity,
             std::format_string<Args...> fmt,
             Args&&... args) const {
    if (!logger_) {
      return;
    }

    logger_->Write(severity, std::format(fmt, std::forward<Args>(args)...));
  }

  [[nodiscard]] log_source with_channel(std::string_view channel) const {
    if (!logger_) {
      return {};
    }
    return log_source{std::make_shared<ProxyLogSink>(logger_, channel)};
  }

 private:
  std::shared_ptr<const LogSink> logger_;
};

}  // namespace transport
