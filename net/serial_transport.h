#pragma once

#include "net/base/net_export.h"
#include "net/asio_transport.h"

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

  SerialTransport(boost::asio::io_context& io_context,
                  std::string device,
                  const Options& options);

  // Transport overrides
  virtual Error Open(Transport::Delegate& delegate) override;
  virtual std::string GetName() const override;
  virtual bool IsActive() const override { return true; }

 private:
  class SerialPortCore;

  const std::string device_;
  const Options options_;
};

}  // namespace net
