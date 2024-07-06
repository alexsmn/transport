#pragma once

#include "net/auto_reset.h"
#include "net/executor.h"
#include "net/logger.h"
#include "net/transport.h"

#include <boost/asio/buffer.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/experimental/as_tuple.hpp>
#include <boost/asio/write.hpp>
#include <boost/circular_buffer.hpp>
#include <memory>

namespace net {

class Logger;

class AsioTransport : public Transport {
 public:
  ~AsioTransport();

  [[nodiscard]] virtual awaitable<Error> Close() override;

  [[nodiscard]] virtual awaitable<ErrorOr<size_t>> Read(
      std::span<char> data) override;

  [[nodiscard]] virtual awaitable<ErrorOr<size_t>> Write(
      std::span<const char> data) override;

  virtual bool IsMessageOriented() const override;
  virtual bool IsConnected() const override;
  virtual Executor GetExecutor() const override;

 protected:
  class Core;

  template <class IoObject>
  class IoCore;

  std::shared_ptr<Core> core_;
};

class AsioTransport::Core {
 public:
  virtual ~Core() {}

  [[nodiscard]] virtual awaitable<Error> Open() = 0;
  [[nodiscard]] virtual awaitable<Error> Close() = 0;
  [[nodiscard]] virtual Executor GetExecutor() = 0;
  [[nodiscard]] virtual bool IsConnected() const = 0;

  [[nodiscard]] virtual awaitable<ErrorOr<std::unique_ptr<Transport>>>
  Accept() = 0;

  [[nodiscard]] virtual awaitable<ErrorOr<size_t>> Read(
      std::span<char> data) = 0;

  [[nodiscard]] virtual awaitable<ErrorOr<size_t>> Write(
      std::span<const char> data) = 0;
};

// AsioTransport::Core

template <class IoObject>
class AsioTransport::IoCore : public Core,
                              public std::enable_shared_from_this<Core> {
 public:
  // Core
  [[nodiscard]] virtual awaitable<Error> Close() override;
  virtual Executor GetExecutor() override { return io_object_.get_executor(); }
  virtual bool IsConnected() const override { return connected_; }

  [[nodiscard]] virtual awaitable<ErrorOr<std::unique_ptr<Transport>>> Accept()
      override;

  [[nodiscard]] virtual awaitable<ErrorOr<size_t>> Read(
      std::span<char> data) override;

  [[nodiscard]] virtual awaitable<ErrorOr<size_t>> Write(
      std::span<const char> data) override;

 protected:
  IoCore(const Executor& executor, std::shared_ptr<const Logger> logger);

  void ProcessError(Error error);

  // Must be called under `io_object_.get_executor()`.
  virtual void Cleanup() = 0;

  const std::shared_ptr<const Logger> logger_;

  IoObject io_object_;

  bool closed_ = false;
  bool connected_ = false;

 private:
  bool reading_ = false;
  bool writing_ = false;
};

template <class IoObject>
inline AsioTransport::IoCore<IoObject>::IoCore(
    const Executor& executor,
    std::shared_ptr<const Logger> logger)
    : logger_{std::move(logger)}, io_object_{executor} {}

template <class IoObject>
inline awaitable<Error> AsioTransport::IoCore<IoObject>::Close() {
  auto ref = shared_from_this();

  co_await boost::asio::post(io_object_.get_executor(),
                             boost::asio::use_awaitable);

  if (closed_) {
    co_return ERR_CONNECTION_CLOSED;
  }

  logger_->WriteF(LogSeverity::Normal, "Close");
  closed_ = true;
  Cleanup();

  co_return OK;
}

template <class IoObject>
inline awaitable<ErrorOr<std::unique_ptr<Transport>>>
AsioTransport::IoCore<IoObject>::Accept() {
  co_return ERR_INVALID_ARGUMENT;
}

template <class IoObject>
inline awaitable<ErrorOr<size_t>> AsioTransport::IoCore<IoObject>::Read(
    std::span<char> data) {
  if (closed_) {
    co_return ERR_CONNECTION_CLOSED;
  }

  if (reading_) {
    co_return ERR_IO_PENDING;
  }

  auto ref = std::static_pointer_cast<IoCore>(shared_from_this());
  AutoReset reading{reading_, true};

  auto [ec, bytes_transferred] = co_await io_object_.async_read_some(
      boost::asio::buffer(data),
      boost::asio::as_tuple(boost::asio::use_awaitable));

  co_return bytes_transferred;
}

template <class IoObject>
inline awaitable<ErrorOr<size_t>> AsioTransport::IoCore<IoObject>::Write(
    std::span<const char> data) {
  if (closed_) {
    co_return ERR_CONNECTION_CLOSED;
  }

  if (writing_) {
    co_return ERR_IO_PENDING;
  }

  auto ref = std::static_pointer_cast<IoCore>(shared_from_this());
  AutoReset writing{writing_, true};

  auto [ec, bytes_transferred] = co_await boost::asio::async_write(
      io_object_, boost::asio::buffer(data),
      boost::asio::as_tuple(boost::asio::use_awaitable));

  if (ec) {
    co_return ec;
  }

  // Per ASIO specs, the number of bytes transferred is always equal to the
  // size of the buffer.
  assert(bytes_transferred == data.size());

  co_return bytes_transferred;
}

template <class IoObject>
inline void AsioTransport::IoCore<IoObject>::ProcessError(Error error) {
  assert(!closed_);

  if (error != OK) {
    logger_->WriteF(LogSeverity::Warning, "Error: %s",
                    ErrorToShortString(error).c_str());
  } else {
    logger_->WriteF(LogSeverity::Normal, "Graceful close");
  }

  closed_ = true;

  Cleanup();
}

// AsioTransport

inline AsioTransport::~AsioTransport() {
  boost::asio::co_spawn(
      core_->GetExecutor(), [core = core_] { return core->Close(); },
      boost::asio::detached);
}

inline awaitable<Error> AsioTransport::Close() {
  return core_->Close();
}

inline awaitable<ErrorOr<size_t>> AsioTransport::Read(std::span<char> data) {
  co_return co_await core_->Read(data);
}

inline awaitable<ErrorOr<size_t>> AsioTransport::Write(
    std::span<const char> data) {
  co_return co_await core_->Write(data);
}

inline bool AsioTransport::IsMessageOriented() const {
  return false;
}

inline bool AsioTransport::IsConnected() const {
  return core_->IsConnected();
}

inline Executor AsioTransport::GetExecutor() const {
  return core_->GetExecutor();
}

}  // namespace net
