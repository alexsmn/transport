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
  virtual int Read(std::span<char> data) override;
  virtual promise<size_t> Write(std::span<const char> data) override;
  virtual bool IsMessageOriented() const override;
  virtual bool IsConnected() const override;

 protected:
  class Core;

  template <class IoObject>
  class IoCore;

  std::shared_ptr<Core> core_;
};

class AsioTransport::Core {
 public:
  virtual ~Core() {}

  virtual Executor GetExecutor() = 0;

  virtual bool IsConnected() const = 0;

  virtual boost::asio::awaitable<void> Open(Handlers handlers) = 0;
  virtual void Close() = 0;

  virtual int Read(std::span<char> data) = 0;
  virtual boost::asio::awaitable<size_t> Write(std::span<const char> data) = 0;
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
  virtual int Read(std::span<char> data) override;

  virtual boost::asio::awaitable<size_t> Write(
      std::span<const char> data) override;

 protected:
  IoCore(const Executor& executor, std::shared_ptr<const Logger> logger);

  boost::asio::awaitable<void> StartReading();
  boost::asio::awaitable<void> StartWriting();

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
  boost::circular_buffer<char> read_buffer_{1024 * 1024};
  std::vector<char> write_buffer_;

  bool reading_ = false;
  std::vector<char> reading_buffer_;

  bool writing_ = false;
  // The buffer being currently written with sync operation.
  std::vector<char> writing_buffer_;
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
inline int AsioTransport::IoCore<IoObject>::Read(std::span<char> data) {
  // Should be only called under the same executor as `io_object_`.
  DFAKE_SCOPED_RECURSIVE_LOCK(mutex_);

  size_t count = std::min(data.size(), read_buffer_.size());
  std::copy(read_buffer_.begin(), read_buffer_.begin() + count, data.data());
  read_buffer_.erase_begin(count);

  // Must start reading, since reading might have stopped if the buffer was
  // full.
  boost::asio::co_spawn(io_object_.get_executor(), StartReading(),
                        boost::asio::detached);

  return count;
}

template <class IoObject>
inline boost::asio::awaitable<size_t> AsioTransport::IoCore<IoObject>::Write(
    std::span<const char> data) {
  auto ref = shared_from_this();

  DFAKE_SCOPED_RECURSIVE_LOCK(mutex_);

  write_buffer_.insert(write_buffer_.end(), data.begin(), data.end());

  boost::asio::co_spawn(io_object_.get_executor(), StartWriting(),
                        boost::asio::detached);

  // TODO: Handle properly.
  co_return data.size();
}

template <class IoObject>
inline boost::asio::awaitable<void>
AsioTransport::IoCore<IoObject>::StartReading() {
  if (closed_ || reading_) {
    co_return;
  }

  auto ref = shared_from_this();
  base::AutoReset reading{&reading_, true};

  for (;;) {
    reading_buffer_.resize(read_buffer_.capacity() - read_buffer_.size());
    if (reading_buffer_.empty()) {
      co_return;
    }

    auto [ec, bytes_transferred] = co_await io_object_.async_read_some(
        boost::asio::buffer(reading_buffer_),
        boost::asio::as_tuple(boost::asio::use_awaitable));

    DFAKE_SCOPED_RECURSIVE_LOCK(mutex_);

    if (closed_) {
      co_return;
    }

    if (ec) {
      if (ec != boost::asio::error::operation_aborted) {
        ProcessError(MapSystemError(ec.value()));
      }
      co_return;
    }

    if (bytes_transferred == 0) {
      ProcessError(OK);
      co_return;
    }

    read_buffer_.insert(read_buffer_.end(), reading_buffer_.begin(),
                        reading_buffer_.begin() + bytes_transferred);
    reading_buffer_.clear();

    // `on_data` may close the transport and reset `handlers_`. It's
    // important to hold a reference to the handler.
    if (auto on_data = handlers_.on_data) {
      on_data();
    }
  }
}

template <class IoObject>
inline boost::asio::awaitable<void>
AsioTransport::IoCore<IoObject>::StartWriting() {
  if (closed_ || writing_) {
    co_return;
  }

  auto ref = shared_from_this();
  base::AutoReset writing{&writing_, true};

  while (!write_buffer_.empty()) {
    writing_buffer_.swap(write_buffer_);

    auto [ec, bytes_transferred] = co_await boost::asio::async_write(
        io_object_, boost::asio::buffer(writing_buffer_),
        boost::asio::as_tuple(boost::asio::use_awaitable));

    DFAKE_SCOPED_RECURSIVE_LOCK(mutex_);

    if (closed_) {
      co_return;
    }

    if (ec) {
      if (ec != boost::asio::error::operation_aborted) {
        ProcessError(MapSystemError(ec.value()));
      }
      co_return;
    }

    assert(bytes_transferred == writing_buffer_.size());
    writing_buffer_.clear();
  }
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

inline int AsioTransport::Read(std::span<char> data) {
  return core_->Read(data);
}

inline promise<size_t> AsioTransport::Write(std::span<const char> data) {
  return to_promise(core_->GetExecutor(), core_->Write(data));
}

inline bool AsioTransport::IsMessageOriented() const {
  return false;
}

inline bool AsioTransport::IsConnected() const {
  return core_->IsConnected();
}

}  // namespace net
