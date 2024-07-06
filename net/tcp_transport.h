#pragma once

#include "net/asio_transport.h"

#include <boost/asio/ip/tcp.hpp>

namespace net {

class TcpTransport final : public AsioTransport {
 public:
  TcpTransport(const Executor& executor,
               std::shared_ptr<const Logger> logger,
               std::string host,
               std::string service,
               bool active);

  // A constructor for a socket accepted by a passive TCP transport.
  // Uses the executor of the socket.
  TcpTransport(std::shared_ptr<const Logger> logger,
               boost::asio::ip::tcp::socket socket);

  ~TcpTransport();

  int GetLocalPort() const;

  // Transport overrides
  [[nodiscard]] virtual awaitable<Error> Open() override;

  [[nodiscard]] virtual awaitable<ErrorOr<std::unique_ptr<Transport>>> Accept()
      override;

  virtual std::string GetName() const override;
  virtual bool IsActive() const override { return type_ == Type::ACTIVE; }

 private:
  class ActiveCore;
  class PassiveCore;

  enum class Type { ACTIVE, PASSIVE, ACCEPTED };
  const Type type_;
};

}  // namespace net
