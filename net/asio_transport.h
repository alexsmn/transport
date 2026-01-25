#pragma once

#include "net/executor.h"
#include "net/logger.h"
#include "net/transport.h"

#include "net/base/threading/thread_collision_warner.h"
#include <boost/asio.hpp>
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

  virtual bool IsConnected() const = 0;

  virtual promise<void> Open(const Handlers& handlers) = 0;
  virtual void Close() = 0;

  virtual int Read(std::span<char> data) = 0;
  virtual promise<size_t> Write(std::span<const char> data) = 0;
};

// AsioTransport::Core

template <class IoObject>
class AsioTransport::IoCore : public Core,
                              public std::enable_shared_from_this<Core> {
 public:
  // Core
  virtual bool IsConnected() const override { return connected_; }
  virtual void Close() override;
  virtual int Read(std::span<char> data) override;
  virtual promise<size_t> Write(std::span<const char> data) override;

 protected:
  IoCore(const Executor& executor, std::shared_ptr<const Logger> logger);

  void StartReading();
  void StartWriting();

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
  StartReading();

  return count;
}

template <class IoObject>
inline promise<size_t> AsioTransport::IoCore<IoObject>::Write(
    std::span<const char> data) {
  promise<size_t> p;

  boost::asio::dispatch(
      io_object_.get_executor(),
      [this, ref = shared_from_this(), p,
       copied_data = std::vector(data.begin(), data.end())]() mutable {
        DFAKE_SCOPED_RECURSIVE_LOCK(mutex_);

        write_buffer_.insert(write_buffer_.end(), copied_data.begin(),
                             copied_data.end());

        StartWriting();

        // TODO: Handle properly.
        p.resolve(copied_data.size());
      });

  return p;
}

template <class IoObject>
inline void AsioTransport::IoCore<IoObject>::StartReading() {
  DFAKE_SCOPED_RECURSIVE_LOCK(mutex_);

  if (closed_ || reading_)
    return;

  assert(reading_buffer_.empty());

  reading_buffer_.resize(read_buffer_.capacity() - read_buffer_.size());
  if (reading_buffer_.empty())
    return;

  reading_ = true;
  boost::asio::async_read(
      io_object_, boost::asio::buffer(reading_buffer_),
      boost::asio::transfer_at_least(1),
      [this, ref = shared_from_this()](const boost::system::error_code& ec,
                                       std::size_t bytes_transferred) {
        DFAKE_SCOPED_RECURSIVE_LOCK(mutex_);

        if (closed_)
          return;

        assert(reading_);
        reading_ = false;

        if (ec) {
          if (ec != boost::asio::error::operation_aborted)
            ProcessError(MapSystemError(ec.value()));
          return;
        }

        if (bytes_transferred == 0) {
          ProcessError(OK);
          return;
        }

        read_buffer_.insert(read_buffer_.end(), reading_buffer_.begin(),
                            reading_buffer_.begin() + bytes_transferred);
        reading_buffer_.clear();

        // `on_data` may close the transport and reset `handlers_`. It's
        // important to hold a reference to the handler.
        if (auto on_data = handlers_.on_data) {
          on_data();
        }

        StartReading();
      });
}

template <class IoObject>
inline void AsioTransport::IoCore<IoObject>::StartWriting() {
  DFAKE_SCOPED_RECURSIVE_LOCK(mutex_);

  if (closed_ || writing_)
    return;

  assert(writing_buffer_.empty());

  if (write_buffer_.empty())
    return;

  writing_ = true;
  writing_buffer_.swap(write_buffer_);

  boost::asio::async_write(
      io_object_, boost::asio::buffer(writing_buffer_),
      [this, ref = shared_from_this()](const boost::system::error_code& ec,
                                       std::size_t bytes_transferred) {
        DFAKE_SCOPED_RECURSIVE_LOCK(mutex_);

        if (closed_)
          return;

        assert(writing_);
        writing_ = false;

        if (ec) {
          if (ec != boost::asio::error::operation_aborted)
            ProcessError(MapSystemError(ec.value()));
          return;
        }

        assert(bytes_transferred == writing_buffer_.size());
        writing_buffer_.clear();

        StartWriting();
      });
}

template <class IoObject>
inline void AsioTransport::IoCore<IoObject>::ProcessError(Error error) {
  boost::asio::dispatch(
      io_object_.get_executor(), [this, ref = shared_from_this(), error] {
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
      });
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
  return core_->Write(data);
}

inline bool AsioTransport::IsMessageOriented() const {
  return false;
}

inline bool AsioTransport::IsConnected() const {
  return core_->IsConnected();
}

}  // namespace net
