#pragma once

#include "transport/asio_transport.h"

#include <boost/asio/ip/tcp.hpp>

namespace transport {

class TcpTransport final : public AsioTransport {
 public:
  TcpTransport(const Executor& executor,
               const log_source& log,
               std::string host,
               std::string service,
               bool active);

  // A constructor for a socket accepted by a passive TCP transport.
  // Uses the executor of the socket.
  TcpTransport(boost::asio::ip::tcp::socket socket, const log_source& log);

  ~TcpTransport();

  int GetLocalPort() const;

  // Transport overrides
  [[nodiscard]] virtual awaitable<Error> open() override;
  [[nodiscard]] virtual awaitable<ErrorOr<any_transport>> accept() override;

  virtual std::string name() const override;
  virtual bool active() const override { return type_ == Type::ACTIVE; }

 private:
  class ActiveCore;
  class PassiveCore;

  enum class Type { ACTIVE, PASSIVE, ACCEPTED };
  const Type type_;
};

}  // namespace transport
