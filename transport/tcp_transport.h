#pragma once

#include "transport/asio_transport.h"

#include <boost/asio/ip/tcp.hpp>

namespace transport {

class ActiveTcpTransport final : public AsioTransport {
 public:
  ActiveTcpTransport(const Executor& executor,
                     const log_source& log,
                     std::string host,
                     std::string service);

  // A constructor for a socket accepted by a passive TCP transport.
  // Uses the executor of the socket.
  ActiveTcpTransport(boost::asio::ip::tcp::socket socket,
                     const log_source& log);

  ~ActiveTcpTransport();

  [[nodiscard]] virtual awaitable<Error> open() override;
  [[nodiscard]] virtual awaitable<ErrorOr<any_transport>> accept() override;

  [[nodiscard]] virtual std::string name() const override;
  [[nodiscard]] virtual bool active() const override {
    return type_ == Type::ACTIVE;
  }

 private:
  class ActiveCore;

  enum class Type { ACTIVE, ACCEPTED };
  const Type type_;
};

class PassiveTcpTransport final : public AsioTransport {
 public:
  PassiveTcpTransport(const Executor& executor,
                      const log_source& log,
                      std::string host,
                      std::string service);

  ~PassiveTcpTransport();

  [[nodiscard]] int GetLocalPort() const;

  [[nodiscard]] virtual awaitable<Error> open() override;
  [[nodiscard]] virtual awaitable<ErrorOr<any_transport>> accept() override;

  [[nodiscard]] virtual std::string name() const override;
  [[nodiscard]] virtual bool active() const override { return false; }

 private:
  class PassiveCore;
};

}  // namespace transport
