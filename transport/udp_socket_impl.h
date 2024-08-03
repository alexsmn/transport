#pragma once

#include "transport/udp_socket.h"

#include <memory>
#include <queue>

namespace transport {

class UdpSocketImpl : private UdpSocketContext,
                      public UdpSocket,
                      public std::enable_shared_from_this<UdpSocketImpl> {
 public:
  explicit UdpSocketImpl(UdpSocketContext&& context);
  ~UdpSocketImpl();

  // UdpSocket
  virtual awaitable<Error> Open() override;
  virtual awaitable<void> Close() override;

  virtual awaitable<ErrorOr<size_t>> SendTo(
      Endpoint endpoint,
      std::span<const char> datagram) override;

 private:
  [[nodiscard]] awaitable<void> StartReading();
  [[nodiscard]] awaitable<void> StartWriting();

  void ProcessError(const boost::system::error_code& ec);

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

inline UdpSocketImpl::~UdpSocketImpl() {
  assert(closed_);
}

inline awaitable<Error> UdpSocketImpl::Open() {
  auto ref = shared_from_this();

  auto [error, iterator] = co_await resolver_.async_resolve(
      /*query=*/{host_, service_},
      boost::asio::as_tuple(boost::asio::use_awaitable));

  if (closed_) {
    co_return ERR_ABORTED;
  }

  if (error) {
    if (error != boost::asio::error::operation_aborted) {
      ProcessError(error);
    }
    co_return error;
  }

  boost::system::error_code ec = boost::asio::error::fault;
  for (Resolver::iterator end; iterator != end; ++iterator) {
    socket_.open(iterator->endpoint().protocol(), ec);
    if (ec) {
      continue;
    }

    if (active_) {
      break;
    }

    socket_.set_option(Socket::reuse_address{true}, ec);
    socket_.bind(iterator->endpoint(), ec);
    if (!ec) {
      break;
    }

    socket_.close();
  }

  if (ec) {
    ProcessError(ec);
    co_return ec;
  }

  connected_ = true;
  open_handler_(iterator->endpoint());

  if (!active_) {
    boost::asio::co_spawn(socket_.get_executor(), StartReading(),
                          boost::asio::detached);
  }

  co_return OK;
}

inline awaitable<void> UdpSocketImpl::Close() {
  auto ref = shared_from_this();

  if (closed_) {
    co_return;
  }

  closed_ = true;
  connected_ = false;
  writing_ = false;
  write_queue_ = {};
  socket_.close();
}

inline awaitable<ErrorOr<size_t>> UdpSocketImpl::SendTo(
    Endpoint endpoint,
    std::span<const char> datagram) {
  auto size = datagram.size();

  write_queue_.emplace(std::piecewise_construct,
                       std::forward_as_tuple(std::move(endpoint)),
                       std::forward_as_tuple(datagram.begin(), datagram.end()));

  boost::asio::co_spawn(socket_.get_executor(), StartWriting(),
                        boost::asio::detached);

  // TODO: Proper async operation.
  co_return size;
}

inline awaitable<void> UdpSocketImpl::StartReading() {
  if (closed_) {
    co_return;
  }

  if (reading_) {
    co_return;
  }

  auto ref = shared_from_this();

  for (;;) {
    reading_ = true;

    auto [error, bytes_transferred] = co_await socket_.async_receive_from(
        boost::asio::buffer(read_buffer_), read_endpoint_,
        boost::asio::as_tuple(boost::asio::use_awaitable));

    reading_ = false;

    if (closed_) {
      co_return;
    }

    if (error) {
      ProcessError(error);
      co_return;
    }

    assert(bytes_transferred != 0);

    std::vector<char> datagram(read_buffer_.begin(),
                               read_buffer_.begin() + bytes_transferred);

    message_handler_(read_endpoint_, std::move(datagram));
  }
}

inline awaitable<void> UdpSocketImpl::StartWriting() {
  if (closed_) {
    co_return;
  }

  if (writing_) {
    co_return;
  }

  auto ref = shared_from_this();

  while (!write_queue_.empty()) {
    writing_ = true;

    auto [endpoint, datagram] = std::move(write_queue_.front());
    write_queue_.pop();

    write_endpoint_ = endpoint;
    write_buffer_ = std::move(datagram);

    auto [error, bytes_transferred] = co_await socket_.async_send_to(
        boost::asio::buffer(write_buffer_), write_endpoint_,
        boost::asio::as_tuple(boost::asio::use_awaitable));

    if (closed_) {
      co_return;
    }

    writing_ = false;
    write_buffer_.clear();
    write_buffer_.shrink_to_fit();

    if (error) {
      ProcessError(error);
      co_return;
    }

    boost::asio::co_spawn(socket_.get_executor(), StartReading(),
                          boost::asio::detached);
  }
}

inline void UdpSocketImpl::ProcessError(const boost::system::error_code& ec) {
  connected_ = false;
  closed_ = true;
  writing_ = false;
  write_queue_ = {};
  error_handler_(ec);
}
}  // namespace transport
