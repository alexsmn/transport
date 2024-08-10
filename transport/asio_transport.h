#pragma once

#include "transport/auto_reset.h"
#include "transport/executor.h"
#include "transport/log.h"
#include "transport/transport.h"

#include <boost/asio/buffer.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/experimental/as_tuple.hpp>
#include <boost/asio/write.hpp>
#include <boost/circular_buffer.hpp>
#include <memory>

namespace transport {

class Logger;

class AsioTransport : public Transport {
 public:
  ~AsioTransport();

  [[nodiscard]] virtual awaitable<Error> close() override;

  [[nodiscard]] virtual awaitable<ErrorOr<size_t>> read(
      std::span<char> data) override;

  [[nodiscard]] virtual awaitable<ErrorOr<size_t>> write(
      std::span<const char> data) override;

  virtual bool message_oriented() const override;
  virtual bool connected() const override;
  virtual Executor get_executor() const override;

 protected:
  class Core;

  template <class IoObject>
  class IoCore;

  std::shared_ptr<Core> core_;
};

class AsioTransport::Core {
 public:
  virtual ~Core() {}

  [[nodiscard]] virtual awaitable<Error> open() = 0;
  [[nodiscard]] virtual awaitable<Error> close() = 0;
  [[nodiscard]] virtual Executor get_executor() = 0;
  [[nodiscard]] virtual bool connected() const = 0;

  [[nodiscard]] virtual awaitable<ErrorOr<any_transport>> accept() = 0;

  [[nodiscard]] virtual awaitable<ErrorOr<size_t>> read(
      std::span<char> data) = 0;

  [[nodiscard]] virtual awaitable<ErrorOr<size_t>> write(
      std::span<const char> data) = 0;
};

// AsioTransport::Core

template <class IoObject>
class AsioTransport::IoCore : public Core,
                              public std::enable_shared_from_this<Core> {
 public:
  // Core
  [[nodiscard]] virtual awaitable<Error> close() override;
  virtual Executor get_executor() override { return io_object_.get_executor(); }
  virtual bool connected() const override { return connected_; }

  [[nodiscard]] virtual awaitable<ErrorOr<any_transport>> accept() override;

  [[nodiscard]] virtual awaitable<ErrorOr<size_t>> read(
      std::span<char> data) override;

  [[nodiscard]] virtual awaitable<ErrorOr<size_t>> write(
      std::span<const char> data) override;

 protected:
  IoCore(const Executor& executor, const log_source& log);

  void ProcessError(Error error);

  // Must be called under `io_object_.get_executor()`.
  virtual void Cleanup() = 0;

  log_source log_;

  IoObject io_object_;

  bool closed_ = false;
  bool connected_ = false;

 private:
  bool reading_ = false;
  bool writing_ = false;
};

template <class IoObject>
inline AsioTransport::IoCore<IoObject>::IoCore(const Executor& executor,
                                               const log_source& log)
    : log_{std::move(log)}, io_object_{executor} {}

template <class IoObject>
inline awaitable<Error> AsioTransport::IoCore<IoObject>::close() {
  auto ref = shared_from_this();

  co_await boost::asio::dispatch(io_object_.get_executor(),
                                 boost::asio::use_awaitable);

  if (closed_) {
    co_return ERR_CONNECTION_CLOSED;
  }

  log_.writef(LogSeverity::Normal, "Close");
  closed_ = true;
  Cleanup();

  co_return OK;
}

template <class IoObject>
inline awaitable<ErrorOr<any_transport>>
AsioTransport::IoCore<IoObject>::accept() {
  co_return ERR_INVALID_ARGUMENT;
}

template <class IoObject>
inline awaitable<ErrorOr<size_t>> AsioTransport::IoCore<IoObject>::read(
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
inline awaitable<ErrorOr<size_t>> AsioTransport::IoCore<IoObject>::write(
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
    log_.writef(LogSeverity::Warning, "Error: %s",
                ErrorToShortString(error).c_str());
  } else {
    log_.writef(LogSeverity::Normal, "Graceful close");
  }

  closed_ = true;

  Cleanup();
}

// AsioTransport

inline AsioTransport::~AsioTransport() {
  boost::asio::co_spawn(
      core_->get_executor(), [core = core_] { return core->close(); },
      boost::asio::detached);
}

inline awaitable<Error> AsioTransport::close() {
  return core_->close();
}

inline awaitable<ErrorOr<size_t>> AsioTransport::read(std::span<char> data) {
  co_return co_await core_->read(data);
}

inline awaitable<ErrorOr<size_t>> AsioTransport::write(
    std::span<const char> data) {
  co_return co_await core_->write(data);
}

inline bool AsioTransport::message_oriented() const {
  return false;
}

inline bool AsioTransport::connected() const {
  return core_->connected();
}

inline Executor AsioTransport::get_executor() const {
  return core_->get_executor();
}

}  // namespace transport
