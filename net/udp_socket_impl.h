#pragma once

#include "base/threading/thread_collision_warner.h"
#include "net/udp_socket.h"

#include <memory>
#include <queue>

namespace net {

class UdpSocketImpl : private UdpSocketContext,
                      public UdpSocket,
                      public std::enable_shared_from_this<UdpSocketImpl> {
 public:
  UdpSocketImpl(UdpSocketContext&& context);

  // UdpSocket
  virtual void Open() override;
  virtual void Close() override;
  virtual promise<size_t> SendTo(const Endpoint& endpoint,
                                 Datagram&& datagram) override;

 private:
  void StartReading();
  void StartWriting();

  void ProcessError(const boost::system::error_code& ec);

  DFAKE_MUTEX(mutex_);

  Resolver resolver_{executor_};
  Socket socket_{executor_};

  bool connected_ = false;
  bool closed_ = false;

  Datagram read_buffer_;
  Endpoint read_endpoint_;
  bool reading_ = false;

  std::queue<std::pair<Endpoint, Datagram>> write_queue_;
  Datagram write_buffer_;
  Endpoint write_endpoint_;
  bool writing_ = false;
};

inline UdpSocketImpl::UdpSocketImpl(UdpSocketContext&& context)
    : UdpSocketContext{std::move(context)}, read_buffer_(1024 * 1024) {}

inline void UdpSocketImpl::Open() {
  resolver_.async_resolve(
      host_, service_,
      [this, ref = shared_from_this()](const boost::system::error_code& error,
                                       Resolver::results_type results) {
        DFAKE_SCOPED_RECURSIVE_LOCK(mutex_);

        if (closed_)
          return;

        if (error) {
          if (error != boost::asio::error::operation_aborted)
            ProcessError(error);
          return;
        }

        boost::system::error_code ec = boost::asio::error::fault;
        Resolver::endpoint_type endpoint;
        for (const auto& entry : results) {
          endpoint = entry.endpoint();

          socket_.open(endpoint.protocol(), ec);
          if (ec)
            continue;

          if (active_)
            break;

          socket_.set_option(Socket::reuse_address{true}, ec);
          socket_.bind(entry.endpoint(), ec);
          if (!ec) {
            break;
          }

          socket_.close();
        }

        if (ec) {
          ProcessError(ec);
          return;
        }

        connected_ = true;
        open_handler_(endpoint);

        if (!active_)
          StartReading();
      });
}

inline void UdpSocketImpl::Close() {
  boost::asio::dispatch(socket_.get_executor(),
                        [this, ref = shared_from_this()] {
                          DFAKE_SCOPED_RECURSIVE_LOCK(mutex_);

                          if (closed_)
                            return;

                          closed_ = true;
                          connected_ = false;
                          writing_ = false;
                          write_queue_ = {};
                          socket_.close();
                        });
}

inline promise<size_t> UdpSocketImpl::SendTo(const Endpoint& endpoint,
                                             Datagram&& datagram) {
  DFAKE_SCOPED_RECURSIVE_LOCK(mutex_);

  auto size = datagram.size();

  write_queue_.emplace(endpoint, std::move(datagram));
  StartWriting();

  // TODO: Property async operation.
  return make_resolved_promise(size);
}

inline void UdpSocketImpl::StartReading() {
  DFAKE_SCOPED_RECURSIVE_LOCK(mutex_);

  if (closed_)
    return;

  if (reading_)
    return;

  reading_ = true;

  socket_.async_receive_from(
      boost::asio::buffer(read_buffer_), read_endpoint_,
      [this, ref = shared_from_this()](const boost::system::error_code& error,
                                       std::size_t bytes_transferred) {
        DFAKE_SCOPED_RECURSIVE_LOCK(mutex_);

        reading_ = false;

        if (closed_)
          return;

        if (error) {
          ProcessError(error);
          return;
        }

        assert(bytes_transferred != 0);

        {
          std::vector<char> datagram(read_buffer_.begin(),
                                     read_buffer_.begin() + bytes_transferred);
          message_handler_(read_endpoint_, std::move(datagram));
        }

        StartReading();
      });
}

inline void UdpSocketImpl::StartWriting() {
  DFAKE_SCOPED_RECURSIVE_LOCK(mutex_);

  if (closed_)
    return;

  if (write_queue_.empty())
    return;

  if (writing_)
    return;

  writing_ = true;

  auto [endpoint, datagram] = std::move(write_queue_.front());
  write_queue_.pop();

  write_endpoint_ = endpoint;
  write_buffer_ = std::move(datagram);

  socket_.async_send_to(
      boost::asio::buffer(write_buffer_), write_endpoint_,
      [this, ref = shared_from_this()](const boost::system::error_code& error,
                                       std::size_t bytes_transferred) {
        DFAKE_SCOPED_RECURSIVE_LOCK(mutex_);

        if (closed_)
          return;

        writing_ = false;
        write_buffer_.clear();
        write_buffer_.shrink_to_fit();

        if (error) {
          ProcessError(error);
          return;
        }

        StartReading();
        StartWriting();
      });
}

inline void UdpSocketImpl::ProcessError(const boost::system::error_code& ec) {
  DFAKE_SCOPED_RECURSIVE_LOCK(mutex_);

  connected_ = false;
  closed_ = true;
  writing_ = false;
  write_queue_ = {};
  error_handler_(ec);
}
}  // namespace net
