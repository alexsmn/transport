#pragma once

#include "net/executor.h"
#include "net/logger.h"
#include "net/transport.h"

#include <base/auto_reset.h>
#include <base/threading/thread_collision_warner.h>
#include <boost/asio.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>
#include <boost/circular_buffer.hpp>
#include <memory>

namespace net {

class Logger;

class AsioTransport : public Transport {
 public:
  ~AsioTransport();

  // Transport overrides
  virtual void Close() override;

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

  [[nodiscard]] virtual Executor GetExecutor() = 0;

  [[nodiscard]] virtual bool IsConnected() const = 0;

  [[nodiscard]] virtual awaitable<Error> Open(Handlers handlers) = 0;

  virtual void Close() = 0;

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
  virtual Executor GetExecutor() override { return io_object_.get_executor(); }
  virtual bool IsConnected() const override { return connected_; }
  virtual void Close() override;

  [[nodiscard]] virtual awaitable<ErrorOr<size_t>> Read(
      std::span<char> data) override;

  [[nodiscard]] virtual awaitable<ErrorOr<size_t>> Write(
      std::span<const char> data) override;

 protected:
  IoCore(const Executor& executor, std::shared_ptr<const Logger> logger);

  void ProcessError(Error error);

  // Must be called under `io_object_.get_executor()`.
  virtual void Cleanup() = 0;

  DFAKE_MUTEX(mutex_);

  const std::shared_ptr<const Logger> logger_;
  Handlers handlers_;

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
inline void AsioTransport::IoCore<IoObject>::Close() {
  boost::asio::dispatch(io_object_.get_executor(),
                        [this, ref = shared_from_this()] {
                          DFAKE_SCOPED_RECURSIVE_LOCK(mutex_);

                          if (closed_) {
                            return;
                          }

                          logger_->WriteF(LogSeverity::Normal, "Close");

                          closed_ = true;
                          handlers_ = {};

                          Cleanup();
                        });
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
  base::AutoReset reading{&reading_, true};

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
  base::AutoReset reading{&writing_, true};

  auto [ec, bytes_transferred] = co_await boost::asio::async_write(
      io_object_, boost::asio::buffer(data),
      boost::asio::as_tuple(boost::asio::use_awaitable));

  if (ec) {
    co_return MapSystemError(ec.value());
  }

  // Per ASIO specs, the number of bytes transferred is always equal to the
  // size of the buffer.
  assert(bytes_transferred == data.size());

  co_return bytes_transferred;
}

template <class IoObject>
inline void AsioTransport::IoCore<IoObject>::ProcessError(Error error) {
  DFAKE_SCOPED_RECURSIVE_LOCK(mutex_);

  assert(!closed_);

  if (error != OK) {
    logger_->WriteF(LogSeverity::Warning, "Error: %s",
                    ErrorToShortString(error).c_str());
  } else {
    logger_->WriteF(LogSeverity::Normal, "Graceful close");
  }

  auto on_close = handlers_.on_close;
  closed_ = true;
  handlers_ = {};

  Cleanup();

  if (on_close)
    on_close(error);
}

// AsioTransport

inline AsioTransport::~AsioTransport() {
  core_->Close();
}

inline void AsioTransport::Close() {
  core_->Close();
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
