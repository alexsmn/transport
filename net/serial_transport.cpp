#include "net/serial_transport.h"

#include "net/base/net_errors.h"

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
                 std::string device,
                 const Options& options);

  // Core
  virtual void Open(Delegate& delegate) override;

 protected:
  virtual void Cleanup() override;

  const std::string device_;
  const Options options_;
};

SerialTransport::SerialPortCore::SerialPortCore(
    boost::asio::io_context& io_context,
    std::string device,
    const Options& options)
    : IoCore{io_context},
      device_{std::move(device)},
      options_{std::move(options)} {}

void SerialTransport::SerialPortCore::Open(Transport::Delegate& delegate) {
  boost::system::error_code ec;
  io_object_.open(device_, ec);
  if (ec) {
    delegate.OnTransportClosed(net::ERR_FAILED);
    return;
  }

  if (!SetOption(io_object_, options_.baud_rate) ||
      !SetOption(io_object_, options_.flow_control) ||
      !SetOption(io_object_, options_.parity) ||
      !SetOption(io_object_, options_.stop_bits) ||
      !SetOption(io_object_, options_.character_size)) {
    io_object_.close(ec);
    delegate.OnTransportClosed(net::ERR_FAILED);
    return;
  }

  connected_ = true;

  delegate_ = &delegate;
  delegate_->OnTransportOpened();

  StartReading();
}

void SerialTransport::SerialPortCore::Cleanup() {
  boost::system::error_code ec;
  io_object_.cancel(ec);
  io_object_.close(ec);

  connected_ = false;
  delegate_ = nullptr;
}

SerialTransport::SerialTransport(boost::asio::io_context& io_context,
                                 std::string device,
                                 const Options& options)
    : AsioTransport{io_context},
      device_{std::move(device)},
      options_{options} {}

net::Error SerialTransport::Open(Transport::Delegate& delegate) {
  core_ = std::make_shared<SerialPortCore>(io_context_, device_, options_);
  core_->Open(delegate);
  return net::OK;
}

std::string SerialTransport::GetName() const {
  return device_;
}

}  // namespace net
