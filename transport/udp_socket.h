#pragma once

#include <boost/asio.hpp>
#include <vector>

namespace transport {

class UdpSocket {
 public:
  using Socket = boost::asio::ip::udp::socket;
  using Resolver = boost::asio::ip::udp::resolver;
  using Endpoint = Socket::endpoint_type;
  using Datagram = std::vector<char>;
  using error_code = boost::system::error_code;

  virtual ~UdpSocket() = default;

  [[nodiscard]] virtual awaitable<error_code> Open() = 0;
  [[nodiscard]] virtual awaitable<void> Close() = 0;

  // Caller must remain the datagram until the operation completes.
  [[nodiscard]] virtual awaitable<expected<size_t>> SendTo(
      Endpoint endpoint,
      std::span<const char> datagram) = 0;

  virtual void Shutdown() = 0;
};

struct UdpSocketContext {
  const executor executor_;
  const std::string host_;
  const std::string service_;
  const bool active_ = false;

  using OpenHandler = std::function<void(const UdpSocket::Endpoint& endpoint)>;
  const OpenHandler open_handler_;

  using MessageHandler = std::function<void(const UdpSocket::Endpoint& endpoint,
                                            UdpSocket::Datagram&& datagram)>;
  const MessageHandler message_handler_;

  using ErrorHandler = std::function<void(const UdpSocket::error_code& error)>;
  const ErrorHandler error_handler_;
};

}  // namespace transport
