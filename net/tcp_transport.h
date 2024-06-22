#pragma once

#include "net/asio_transport.h"

namespace net {

class AsioTcpTransport final : public AsioTransport {
 public:
  AsioTcpTransport(const Executor& executor,
                   std::shared_ptr<const Logger> logger,
                   std::string host,
                   std::string service,
                   bool active);

  // A constructor for a socket accepted by a passive TCP transport.
  // Uses the executor of the socket.
  AsioTcpTransport(std::shared_ptr<const Logger> logger,
                   boost::asio::ip::tcp::socket socket);

  ~AsioTcpTransport();

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
