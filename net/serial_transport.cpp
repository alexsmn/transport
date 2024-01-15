#include "net/serial_transport.h"

#include "net/base/net_errors.h"
#include "net/transport_util.h"

#include <boost/asio/serial_port.hpp>

using namespace std::chrono_literals;

namespace net {

namespace {

template <class T>
inline bool SetOption(boost::asio::serial_port& serial_port,
                      const std::optional<T>& option) {
  if (!option.has_value())
    return true;

  boost::system::error_code ec;
  serial_port.set_option(*option, ec);
  return !ec;
}

}  // namespace

class SerialTransport::SerialPortCore final
    : public AsioTransport::IoCore<boost::asio::serial_port> {
 public:
  SerialPortCore(boost::asio::io_context& io_context,
                 std::shared_ptr<const Logger> logger,
                 std::string device,
                 const Options& options);

  // Core
  virtual void Open(const Handlers& handlers) override;

 protected:
  virtual void Cleanup() override;

  const std::string device_;
  const Options options_;
};

SerialTransport::SerialPortCore::SerialPortCore(
    boost::asio::io_context& io_context,
    std::shared_ptr<const Logger> logger,
    std::string device,
    const Options& options)
    : IoCore{io_context, std::move(logger)},
      device_{std::move(device)},
      options_{std::move(options)} {}

void SerialTransport::SerialPortCore::Open(const Handlers& handlers) {
  boost::system::error_code ec;
  io_object_.open(device_, ec);
  if (ec) {
    if (handlers.on_close)
      handlers.on_close(net::ERR_FAILED);
    return;
  }

  if (!SetOption(io_object_, options_.baud_rate) ||
      !SetOption(io_object_, options_.flow_control) ||
      !SetOption(io_object_, options_.parity) ||
      !SetOption(io_object_, options_.stop_bits) ||
      !SetOption(io_object_, options_.character_size)) {
    io_object_.close(ec);
    if (handlers.on_close)
      handlers.on_close(net::ERR_FAILED);
    return;
  }

  connected_ = true;

  handlers_ = handlers;
  if (handlers_.on_open)
    handlers_.on_open();

  StartReading();
}

void SerialTransport::SerialPortCore::Cleanup() {
  assert(closed_);

  connected_ = false;

  boost::system::error_code ec;
  io_object_.cancel(ec);
  io_object_.close(ec);
}

SerialTransport::SerialTransport(boost::asio::io_context& io_context,
                                 std::shared_ptr<const Logger> logger,
                                 std::string device,
                                 const Options& options)
    : io_context_{io_context},
      logger_{std::move(logger)},
      device_{std::move(device)},
      options_{options} {}

promise<void> SerialTransport::Open(const Handlers& handlers) {
  auto [p, promise_handlers] = MakePromiseHandlers(handlers);

  core_ =
      std::make_shared<SerialPortCore>(io_context_, logger_, device_, options_);
  core_->Open(promise_handlers);

  return p;
}

std::string SerialTransport::GetName() const {
  return device_;
}

}  // namespace net
