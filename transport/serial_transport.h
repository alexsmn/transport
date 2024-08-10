#pragma once

#include "transport/asio_transport.h"

#include <boost/asio/serial_port.hpp>
#include <optional>

namespace transport {

class SerialTransport final : public AsioTransport {
 public:
  struct Options {
    std::optional<boost::asio::serial_port::baud_rate> baud_rate;
    std::optional<boost::asio::serial_port::flow_control> flow_control;
    std::optional<boost::asio::serial_port::parity> parity;
    std::optional<boost::asio::serial_port::stop_bits> stop_bits;
    std::optional<boost::asio::serial_port::character_size> character_size;
  };

  SerialTransport(const Executor& executor,
                  const log_source& log,
                  std::string device,
                  const Options& options);

  // Transport overrides
  [[nodiscard]] virtual awaitable<Error> open() override;
  [[nodiscard]] virtual awaitable<ErrorOr<any_transport>> accept() override;
  virtual std::string name() const override;
  virtual bool active() const override { return true; }

 private:
  class SerialPortCore;
};

}  // namespace transport
