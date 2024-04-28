#include "net/serial_transport.h"

#include "net/base/net_errors.h"
#include "net/net_exception.h"

#include <boost/asio/serial_port.hpp>

using namespace std::chrono_literals;

namespace net {

namespace {

template <class T>
inline bool SetOption(boost::asio::serial_port& serial_port,
                      const std::optional<T>& option) {
  if (!option.has_value()) {
    return true;
  }

  boost::system::error_code ec;
  serial_port.set_option(*option, ec);
  return !ec;
}

}  // namespace

class SerialTransport::SerialPortCore final
    : public AsioTransport::IoCore<boost::asio::serial_port> {
 public:
  SerialPortCore(const Executor& executor,
                 std::shared_ptr<const Logger> logger,
                 std::string device,
                 const Options& options);

  // Core
  virtual boost::asio::awaitable<void> Open(Handlers handlers) override;

 protected:
  virtual void Cleanup() override;

  const std::string device_;
  const Options options_;
};

SerialTransport::SerialPortCore::SerialPortCore(
    const Executor& executor,
    std::shared_ptr<const Logger> logger,
    std::string device,
    const Options& options)
    : IoCore{executor, std::move(logger)},
      device_{std::move(device)},
      options_{options} {}

boost::asio::awaitable<void> SerialTransport::SerialPortCore::Open(
    Handlers handlers) {
  auto ref = std::static_pointer_cast<SerialPortCore>(shared_from_this());

  boost::system::error_code ec;
  io_object_.open(device_, ec);
  if (ec) {
    if (handlers.on_close) {
      handlers.on_close(ERR_FAILED);
    }
    throw net_exception{ERR_FAILED};
  }

  if (!SetOption(io_object_, options_.baud_rate) ||
      !SetOption(io_object_, options_.flow_control) ||
      !SetOption(io_object_, options_.parity) ||
      !SetOption(io_object_, options_.stop_bits) ||
      !SetOption(io_object_, options_.character_size)) {
    io_object_.close(ec);
    if (handlers.on_close) {
      handlers.on_close(ERR_FAILED);
    }
    throw net_exception{ERR_FAILED};
  }

  connected_ = true;

  handlers_ = std::move(handlers);

  if (handlers_.on_open) {
    handlers_.on_open();
  }

  boost::asio::co_spawn(io_object_.get_executor(),
                        std::bind_front(&SerialPortCore::StartReading, ref),
                        boost::asio::detached);

  co_return;
}

void SerialTransport::SerialPortCore::Cleanup() {
  assert(closed_);

  connected_ = false;

  boost::system::error_code ec;
  io_object_.cancel(ec);
  io_object_.close(ec);
}

SerialTransport::SerialTransport(const Executor& executor,
                                 std::shared_ptr<const Logger> logger,
                                 std::string device,
                                 const Options& options)
    : executor_{executor},
      logger_{std::move(logger)},
      device_{std::move(device)},
      options_{options} {}

boost::asio::awaitable<void> SerialTransport::Open(Handlers handlers) {
  core_ =
      std::make_shared<SerialPortCore>(executor_, logger_, device_, options_);

  return core_->Open(handlers);
}

std::string SerialTransport::GetName() const {
  return device_;
}

}  // namespace net
