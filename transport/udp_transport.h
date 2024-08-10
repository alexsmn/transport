#pragma once

#include "transport/asio_transport.h"
#include "transport/udp_socket_factory.h"

namespace transport {

class Logger;

class UdpTransport final : public Transport {
 public:
  UdpTransport(const Executor& executor,
               const log_source& log,
               UdpSocketFactory udp_socket_factory,
               std::string host,
               std::string service,
               bool active);

  ~UdpTransport();

  [[nodiscard]] virtual std::string name() const override;
  [[nodiscard]] virtual bool active() const override { return active_; }
  [[nodiscard]] virtual bool connected() const override;
  [[nodiscard]] virtual bool message_oriented() const override { return true; }
  [[nodiscard]] virtual Executor get_executor() override;

  [[nodiscard]] virtual awaitable<Error> open() override;
  [[nodiscard]] virtual awaitable<Error> close() override;
  [[nodiscard]] virtual awaitable<ErrorOr<any_transport>> accept() override;

  [[nodiscard]] virtual awaitable<ErrorOr<size_t>> read(
      std::span<char> data) override;

  [[nodiscard]] virtual awaitable<ErrorOr<size_t>> write(
      std::span<const char> data) override;

 private:
  class Core;
  class UdpActiveCore;
  class UdpPassiveCore;
  class UdpAcceptedCore;

  explicit UdpTransport(std::shared_ptr<Core> core);

  bool active_ = false;
  std::shared_ptr<Core> core_;
};

}  // namespace transport
