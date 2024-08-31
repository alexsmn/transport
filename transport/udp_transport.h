#pragma once

#include "transport/log.h"
#include "transport/transport.h"
#include "transport/udp_socket_factory.h"

namespace transport {

class AcceptedUdpTransport;

class ActiveUdpTransport final : public Transport {
 public:
  ActiveUdpTransport(const Executor& executor,
                     const log_source& log,
                     UdpSocketFactory udp_socket_factory,
                     std::string host,
                     std::string service);

  ~ActiveUdpTransport();

  [[nodiscard]] virtual std::string name() const override;
  [[nodiscard]] virtual bool active() const override { return true; }
  [[nodiscard]] virtual bool connected() const override;
  [[nodiscard]] virtual bool message_oriented() const override { return true; }
  [[nodiscard]] virtual Executor get_executor() override;

  [[nodiscard]] virtual awaitable<error_code> open() override;
  [[nodiscard]] virtual awaitable<error_code> close() override;
  [[nodiscard]] virtual awaitable<expected<any_transport>> accept() override;

  [[nodiscard]] virtual awaitable<expected<size_t>> read(
      std::span<char> data) override;

  [[nodiscard]] virtual awaitable<expected<size_t>> write(
      std::span<const char> data) override;

 private:
  class UdpActiveCore;

  std::shared_ptr<UdpActiveCore> core_;
};

class PassiveUdpTransport final : public Transport {
 public:
  PassiveUdpTransport(const Executor& executor,
                      const log_source& log,
                      UdpSocketFactory udp_socket_factory,
                      std::string host,
                      std::string service);

  ~PassiveUdpTransport();

  [[nodiscard]] virtual std::string name() const override;
  [[nodiscard]] virtual bool active() const override { return false; }
  [[nodiscard]] virtual bool connected() const override;
  [[nodiscard]] virtual bool message_oriented() const override { return true; }
  [[nodiscard]] virtual Executor get_executor() override;

  [[nodiscard]] virtual awaitable<error_code> open() override;
  [[nodiscard]] virtual awaitable<error_code> close() override;
  [[nodiscard]] virtual awaitable<expected<any_transport>> accept() override;

  [[nodiscard]] virtual awaitable<expected<size_t>> read(
      std::span<char> data) override;

  [[nodiscard]] virtual awaitable<expected<size_t>> write(
      std::span<const char> data) override;

 private:
  class UdpPassiveCore;

  std::shared_ptr<UdpPassiveCore> core_;

  friend class AcceptedUdpTransport;
};

}  // namespace transport
