#pragma once

#include "transport/auto_reset.h"
#include "transport/executor.h"
#include "transport/log.h"
#include "transport/transport.h"

#include <boost/asio/buffer.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/experimental/as_tuple.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/write.hpp>
#include <memory>

namespace transport {

class Logger;

template <class IoObject>
class AsioTransport : public Transport {
 public:
  [[nodiscard]] virtual bool message_oriented() const override { return false; }

  [[nodiscard]] virtual Executor get_executor() override {
    return io_object_.get_executor();
  }

  [[nodiscard]] virtual bool connected() const override { return connected_; }

  [[nodiscard]] virtual awaitable<Error> close() override;

  [[nodiscard]] virtual awaitable<ErrorOr<any_transport>> accept() override;

  [[nodiscard]] virtual awaitable<ErrorOr<size_t>> read(
      std::span<char> data) override;

  [[nodiscard]] virtual awaitable<ErrorOr<size_t>> write(
      std::span<const char> data) override;

 protected:
  AsioTransport(const Executor& executor, const log_source& log);

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
inline AsioTransport<IoObject>::AsioTransport(const Executor& executor,
                                              const log_source& log)
    : log_{std::move(log)}, io_object_{executor} {}

template <class IoObject>
inline awaitable<Error> AsioTransport<IoObject>::close() {
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
inline awaitable<ErrorOr<any_transport>> AsioTransport<IoObject>::accept() {
  co_return ERR_INVALID_ARGUMENT;
}

template <class IoObject>
inline awaitable<ErrorOr<size_t>> AsioTransport<IoObject>::read(
    std::span<char> data) {
  if (closed_) {
    co_return ERR_CONNECTION_CLOSED;
  }

  if (reading_) {
    co_return ERR_IO_PENDING;
  }

  AutoReset reading{reading_, true};

  auto [ec, bytes_transferred] = co_await io_object_.async_read_some(
      boost::asio::buffer(data),
      boost::asio::as_tuple(boost::asio::use_awaitable));

  if (ec) {
    co_return ec;
  }

  co_return bytes_transferred;
}

template <class IoObject>
inline awaitable<ErrorOr<size_t>> AsioTransport<IoObject>::write(
    std::span<const char> data) {
  if (closed_) {
    co_return ERR_CONNECTION_CLOSED;
  }

  if (writing_) {
    co_return ERR_IO_PENDING;
  }

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
inline void AsioTransport<IoObject>::ProcessError(Error error) {
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

}  // namespace transport
