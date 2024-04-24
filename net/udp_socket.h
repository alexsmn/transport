#pragma once

#include <boost/asio.hpp>
#include <vector>

namespace net {

class UdpSocket {
 public:
  using Socket = boost::asio::ip::udp::socket;
  using Resolver = boost::asio::ip::udp::resolver;
  using Endpoint = Socket::endpoint_type;
  using Datagram = std::vector<char>;
  using Error = boost::system::error_code;

  virtual ~UdpSocket() = default;

  virtual void Open() = 0;
  virtual void Close() = 0;
  virtual boost::asio::awaitable<size_t> SendTo(const Endpoint& endpoint,
                                                Datagram datagram) = 0;
};

struct UdpSocketContext {
  const Executor executor_;
  const std::string host_;
  const std::string service_;
  const bool active_ = false;

  using OpenHandler = std::function<void(const UdpSocket::Endpoint& endpoint)>;
  const OpenHandler open_handler_;

  using MessageHandler = std::function<void(const UdpSocket::Endpoint& endpoint,
                                            UdpSocket::Datagram&& datagram)>;
  const MessageHandler message_handler_;

  using ErrorHandler = std::function<void(const UdpSocket::Error& error)>;
  const ErrorHandler error_handler_;
};

}  // namespace net
