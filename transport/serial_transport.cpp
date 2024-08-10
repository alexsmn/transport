#include "transport/serial_transport.h"

#include "transport/error.h"

#include <boost/asio/serial_port.hpp>

using namespace std::chrono_literals;

namespace transport {

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
  SerialPortCore(const Executor& executor,
                 const log_source& log,
                 std::string device,
                 const Options& options);

  const std::string& device() const { return device_; }

  // Core
  virtual awaitable<Error> Open() override;

 protected:
  virtual void Cleanup() override;

  const std::string device_;
  const Options options_;
};

SerialTransport::SerialPortCore::SerialPortCore(const Executor& executor,
                                                const log_source& log,
                                                std::string device,
                                                const Options& options)
    : IoCore{executor, std::move(log)},
      device_{std::move(device)},
      options_{options} {}

awaitable<Error> SerialTransport::SerialPortCore::Open() {
  auto ref = std::static_pointer_cast<SerialPortCore>(shared_from_this());

  boost::system::error_code ec;
  io_object_.open(device_, ec);
  if (ec) {
    co_return ERR_FAILED;
  }

  if (!SetOption(io_object_, options_.baud_rate) ||
      !SetOption(io_object_, options_.flow_control) ||
      !SetOption(io_object_, options_.parity) ||
      !SetOption(io_object_, options_.stop_bits) ||
      !SetOption(io_object_, options_.character_size)) {
    io_object_.close(ec);
    co_return ERR_FAILED;
  }

  connected_ = true;

  co_return OK;
}

void SerialTransport::SerialPortCore::Cleanup() {
  assert(closed_);

  connected_ = false;

  boost::system::error_code ec;
  io_object_.cancel(ec);
  io_object_.close(ec);
}

SerialTransport::SerialTransport(const Executor& executor,
                                 const log_source& log,
                                 std::string device,
                                 const Options& options) {
  core_ = std::make_shared<SerialPortCore>(executor, log, device, options);
}

awaitable<Error> SerialTransport::open() {
  if (!core_) {
    co_return ERR_INVALID_HANDLE;
  }

  co_return co_await core_->Open();
}

std::string SerialTransport::name() const {
  return core_ ? std::static_pointer_cast<SerialPortCore>(core_)->device()
               : std::string{};
}

awaitable<ErrorOr<any_transport>> SerialTransport::accept() {
  co_return ERR_ACCESS_DENIED;
}

}  // namespace transport
