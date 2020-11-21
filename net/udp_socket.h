#pragma once

#include <boost/asio.hpp>
#include <memory>
#include <queue>

namespace net {

struct UdpSocketContext {
  using Socket = boost::asio::ip::udp::socket;
  using Endpoint = Socket::endpoint_type;
  using Datagram = std::vector<char>;
  using Error = boost::system::error_code;

  boost::asio::io_context& io_context_;
  const std::string host_;
  const std::string service_;
  const bool active_ = false;

  const std::function<void(const Endpoint& endpoint)> open_handler_;
  const std::function<void(const Endpoint& endpoint, Datagram&& datagram)>
      message_handler_;
  const std::function<void(const Error& error)> error_handler_;
};

class UdpSocket : private UdpSocketContext,
                  public std::enable_shared_from_this<UdpSocket> {
 public:
  using Socket = boost::asio::ip::udp::socket;
  using Resolver = boost::asio::ip::udp::resolver;
  using Endpoint = Socket::endpoint_type;
  using Datagram = std::vector<char>;

  explicit UdpSocket(UdpSocketContext&& context);

  void Open();
  void Close();

  void SendTo(const Endpoint& endpoint, Datagram&& datagram);

  void StartReading();
  void StartWriting();

  void ProcessError(const boost::system::error_code& ec);

  Resolver resolver_{io_context_};
  Socket socket_{io_context_};

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

inline UdpSocket::UdpSocket(UdpSocketContext&& context)
    : UdpSocketContext{std::move(context)}, read_buffer_(1024 * 1024) {}

inline void UdpSocket::Open() {
  Resolver::query query{host_, service_};
  resolver_.async_resolve(query, [this, ref = shared_from_this()](
                                     const boost::system::error_code& error,
                                     Resolver::iterator iterator) {
    if (closed_)
      return;

    if (error) {
      if (error != boost::asio::error::operation_aborted)
        ProcessError(error);
      return;
    }

    boost::system::error_code ec = boost::asio::error::fault;
    for (Resolver::iterator end; iterator != end; ++iterator) {
      socket_.open(iterator->endpoint().protocol(), ec);
      if (ec)
        continue;

      if (active_)
        break;

      socket_.set_option(Socket::reuse_address{true}, ec);
      socket_.bind(iterator->endpoint(), ec);
      if (!ec)
        break;

      socket_.close();
    }

    if (ec) {
      ProcessError(ec);
      return;
    }

    connected_ = true;
    open_handler_(iterator->endpoint());

    if (!active_)
      StartReading();
  });
}

inline void UdpSocket::Close() {
  if (closed_)
    return;

  closed_ = true;
  connected_ = false;
  writing_ = false;
  write_queue_ = {};
  socket_.close();
}

inline void UdpSocket::SendTo(const Endpoint& endpoint, Datagram&& datagram) {
  write_queue_.emplace(std::make_pair(endpoint, std::move(datagram)));
  StartWriting();
}

inline void UdpSocket::StartReading() {
  if (closed_)
    return;

  if (reading_)
    return;

  reading_ = true;

  socket_.async_receive_from(
      boost::asio::buffer(read_buffer_), read_endpoint_,
      [this, ref = shared_from_this()](const boost::system::error_code& error,
                                       std::size_t bytes_transferred) {
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

inline void UdpSocket::StartWriting() {
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

inline void UdpSocket::ProcessError(const boost::system::error_code& ec) {
  connected_ = false;
  closed_ = true;
  writing_ = false;
  write_queue_ = {};
  error_handler_(ec);
}
}  // namespace net
