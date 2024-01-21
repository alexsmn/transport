#pragma once

#include "net/asio_transport.h"
#include "net/base/net_export.h"

#include <optional>

namespace net {

class NET_EXPORT SerialTransport final : public AsioTransport {
 public:
  struct Options {
    std::optional<boost::asio::serial_port::baud_rate> baud_rate;
    std::optional<boost::asio::serial_port::flow_control> flow_control;
    std::optional<boost::asio::serial_port::parity> parity;
    std::optional<boost::asio::serial_port::stop_bits> stop_bits;
    std::optional<boost::asio::serial_port::character_size> character_size;
  };

  SerialTransport(const Executor& executor,
                  std::shared_ptr<const Logger> logger,
                  std::string device,
                  const Options& options);

  // Transport overrides
  virtual promise<void> Open(const Handlers& handlers) override;
  virtual std::string GetName() const override;
  virtual bool IsActive() const override { return true; }

 private:
  class SerialPortCore;

  Executor executor_;
  const std::shared_ptr<const Logger> logger_;

  const std::string device_;
  const Options options_;
};

}  // namespace net
