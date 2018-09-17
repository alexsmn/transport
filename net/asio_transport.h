#pragma once

#include "net/transport.h"

#include <boost/asio.hpp>
#include <boost/circular_buffer.hpp>
#include <memory>

namespace net {

class AsioTransport : public Transport {
 public:
  explicit AsioTransport(boost::asio::io_context& io_context);
  ~AsioTransport();

  // Transport overrides
  virtual void Close() override;
  virtual int Read(void* data, size_t len) override;
  virtual int Write(const void* data, size_t len) override;
  virtual bool IsMessageOriented() const override;
  virtual bool IsConnected() const override;

 protected:
  class Core;

  template <class IoObject>
  class IoCore;

  boost::asio::io_context& io_context_;
  std::shared_ptr<Core> core_;
};

class AsioTransport::Core {
 public:
  virtual ~Core() {}

  virtual bool IsConnected() const = 0;

  virtual void Open(Delegate& delegate) = 0;
  virtual void Close() = 0;

  virtual int Read(void* data, size_t len) = 0;
  virtual int Write(const void* data, size_t len) = 0;
};

// AsioTransport::Core

template <class IoObject>
class AsioTransport::IoCore : public Core,
                              public std::enable_shared_from_this<Core> {
 public:
  // Core
  virtual bool IsConnected() const override { return connected_; }
  virtual void Close() override;
  virtual int Read(void* data, size_t len) override;
  virtual int Write(const void* data, size_t len) override;

 protected:
  explicit IoCore(boost::asio::io_context& io_context);

  void StartReading();
  void StartWriting();

  void ProcessError(net::Error error);

  virtual void Cleanup() = 0;

  boost::asio::io_context& io_context_;
  Delegate* delegate_ = nullptr;

  IoObject io_object_;

  bool closed_ = false;
  bool connected_ = false;

 private:
  boost::circular_buffer<char> read_buffer_{1024 * 1024};
  std::vector<char> write_buffer_;

  bool reading_ = false;
  std::vector<char> reading_buffer_;

  bool writing_ = false;
  // The buffer being curently written with sync operation.
  std::vector<char> writing_buffer_;
};

template <class IoObject>
inline AsioTransport::IoCore<IoObject>::IoCore(
    boost::asio::io_context& io_context)
    : io_context_{io_context}, io_object_{io_context} {}

template <class IoObject>
inline void AsioTransport::IoCore<IoObject>::Close() {
  closed_ = true;
  delegate_ = nullptr;

  Cleanup();
}

template <class IoObject>
inline int AsioTransport::IoCore<IoObject>::Read(void* data, size_t len) {
  size_t count = std::min(len, read_buffer_.size());
  std::copy(read_buffer_.begin(), read_buffer_.begin() + count,
            reinterpret_cast<char*>(data));
  read_buffer_.erase_begin(count);

  StartReading();

  return count;
}

template <class IoObject>
inline int AsioTransport::IoCore<IoObject>::Write(const void* data,
                                                  size_t len) {
  write_buffer_.insert(write_buffer_.end(), reinterpret_cast<const char*>(data),
                       reinterpret_cast<const char*>(data) + len);

  StartWriting();

  return len;
}

template <class IoObject>
inline void AsioTransport::IoCore<IoObject>::StartReading() {
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
        if (closed_)
          return;

        assert(reading_);
        reading_ = false;

        if (ec) {
          if (ec != boost::asio::error::operation_aborted)
            ProcessError(net::MapSystemError(ec.value()));
          return;
        }

        if (bytes_transferred == 0) {
          ProcessError(net::OK);
          return;
        }

        read_buffer_.insert(read_buffer_.end(), reading_buffer_.begin(),
                            reading_buffer_.begin() + bytes_transferred);
        reading_buffer_.clear();

        delegate_->OnTransportDataReceived();

        StartReading();
      });
}

template <class IoObject>
inline void AsioTransport::IoCore<IoObject>::StartWriting() {
  if (closed_ || writing_)
    return;

  assert(writing_buffer_.empty());

  if (write_buffer_.empty())
    return;

  writing_ = true;
  writing_buffer_.swap(write_buffer_);

  boost::system::error_code ec;
  boost::asio::async_write(
      io_object_, boost::asio::buffer(writing_buffer_),
      [this, ref = shared_from_this()](const boost::system::error_code& ec,
                                       std::size_t bytes_transferred) {
        if (closed_)
          return;

        assert(writing_);
        writing_ = false;

        if (ec) {
          if (ec != boost::asio::error::operation_aborted)
            ProcessError(net::MapSystemError(ec.value()));
          return;
        }

        assert(bytes_transferred == writing_buffer_.size());
        writing_buffer_.clear();

        StartWriting();
      });
}

template <class IoObject>
inline void AsioTransport::IoCore<IoObject>::ProcessError(net::Error error) {
  auto* delegate = delegate_;
  closed_ = true;
  delegate_ = nullptr;

  Cleanup();

  delegate->OnTransportClosed(error);
}

// AsioTransport

inline AsioTransport::AsioTransport(boost::asio::io_context& io_context)
    : io_context_{io_context} {}

inline AsioTransport::~AsioTransport() {
  if (core_)
    core_->Close();
}

inline void AsioTransport::Close() {
  core_->Close();
  core_ = nullptr;
}

inline int AsioTransport::Read(void* data, size_t len) {
  return core_ ? core_->Read(data, len) : net::ERR_FAILED;
}

inline int AsioTransport::Write(const void* data, size_t len) {
  return core_ ? core_->Write(data, len) : net::ERR_FAILED;
}

inline bool AsioTransport::IsMessageOriented() const {
  return false;
}

inline bool AsioTransport::IsConnected() const {
  return core_ && core_->IsConnected();
}

}  // namespace net
