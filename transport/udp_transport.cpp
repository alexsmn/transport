#include "transport/udp_transport.h"

#include "transport/any_transport.h"
#include "transport/log.h"
#include "transport/udp_socket_impl.h"

#include <boost/asio/experimental/as_tuple.hpp>
#include <boost/asio/experimental/channel.hpp>
#include <map>
#include <ranges>

std::string ToString(const transport::UdpSocket::Endpoint& endpoint) {
  std::stringstream stream;
  stream << endpoint;
  return stream.str();
}

namespace transport {

// AsioUdpTransport::UdpActiveCore

class AsioUdpTransport::UdpActiveCore final
    : public Core,
      public std::enable_shared_from_this<UdpActiveCore> {
 public:
  UdpActiveCore(const Executor& executor,
                UdpSocketFactory udp_socket_factory,
                std::string host,
                std::string service);

  // Core
  virtual Executor get_executor() override { return executor_; }
  virtual bool connected() const override { return connected_; }

  [[nodiscard]] virtual awaitable<Error> Open() override;
  [[nodiscard]] virtual awaitable<Error> Close() override;

  [[nodiscard]] virtual awaitable<ErrorOr<any_transport>> Accept() override;

  [[nodiscard]] virtual awaitable<ErrorOr<size_t>> Read(
      std::span<char> data) override;

  [[nodiscard]] virtual awaitable<ErrorOr<size_t>> Write(
      std::span<const char> data) override;

 private:
  UdpSocketContext MakeUdpSocketImplContext();

  void OnSocketOpened(const UdpSocket::Endpoint& endpoint);
  void OnSocketMessage(const UdpSocket::Endpoint& endpoint,
                       UdpSocket::Datagram&& datagram);
  void OnSocketClosed(const UdpSocket::Error& error);

  const Executor executor_;
  const UdpSocketFactory udp_socket_factory_;
  const std::string host_;
  const std::string service_;

  std::shared_ptr<UdpSocket> socket_;

  bool connected_ = false;
  UdpSocket::Endpoint peer_endpoint_;
};

AsioUdpTransport::UdpActiveCore::UdpActiveCore(
    const Executor& executor,
    UdpSocketFactory udp_socket_factory,
    std::string host,
    std::string service)
    : executor_{executor},
      udp_socket_factory_{std::move(udp_socket_factory)},
      host_{std::move(host)},
      service_{std::move(service)} {}

awaitable<Error> AsioUdpTransport::UdpActiveCore::Open() {
  socket_ = udp_socket_factory_(MakeUdpSocketImplContext());

  return socket_->Open();
}

awaitable<Error> AsioUdpTransport::UdpActiveCore::Close() {
  auto ref = shared_from_this();

  co_await boost::asio::dispatch(executor_, boost::asio::use_awaitable);

  connected_ = false;

  co_await socket_->Close();

  co_return OK;
}

awaitable<ErrorOr<any_transport>> AsioUdpTransport::UdpActiveCore::Accept() {
  co_return ERR_INVALID_ARGUMENT;
}

awaitable<ErrorOr<size_t>> AsioUdpTransport::UdpActiveCore::Read(
    std::span<char> data) {
  co_return ERR_FAILED;
}

awaitable<ErrorOr<size_t>> AsioUdpTransport::UdpActiveCore::Write(
    std::span<const char> data) {
  return socket_->SendTo(peer_endpoint_, data);
}

void AsioUdpTransport::UdpActiveCore::OnSocketOpened(
    const UdpSocket::Endpoint& endpoint) {
  peer_endpoint_ = endpoint;
  connected_ = true;
}

void AsioUdpTransport::UdpActiveCore::OnSocketMessage(
    const UdpSocket::Endpoint& endpoint,
    UdpSocket::Datagram&& datagram) {
  /*if (handlers_.on_message) {
    handlers_.on_message(datagram);
  }*/
}

void AsioUdpTransport::UdpActiveCore::OnSocketClosed(
    const UdpSocket::Error& error) {
  connected_ = false;
}

UdpSocketContext AsioUdpTransport::UdpActiveCore::MakeUdpSocketImplContext() {
  return {
      executor_,
      host_,
      service_,
      true,
      [weak_ptr = weak_from_this()](const UdpSocket::Endpoint& endpoint) {
        if (auto ref = weak_ptr.lock())
          ref->OnSocketOpened(endpoint);
      },
      [weak_ptr = weak_from_this()](const UdpSocket::Endpoint& endpoint,
                                    UdpSocket::Datagram&& datagram) {
        if (auto ref = weak_ptr.lock())
          ref->OnSocketMessage(endpoint, std::move(datagram));
      },
      [weak_ptr = weak_from_this()](const UdpSocket::Error& error) {
        if (auto ref = weak_ptr.lock())
          ref->OnSocketClosed(error);
      },
  };
}

// AsioUdpTransport::UdpAcceptedCore

class AsioUdpTransport::UdpAcceptedCore final
    : public Core,
      public std::enable_shared_from_this<UdpAcceptedCore> {
 public:
  UdpAcceptedCore(const Executor& executor,
                  const log_source& log,
                  std::shared_ptr<UdpPassiveCore> passive_core,
                  UdpSocket::Endpoint endpoint);
  ~UdpAcceptedCore();

  // Core
  virtual Executor get_executor() override { return executor_; }
  virtual bool connected() const override { return connected_; }
  virtual awaitable<Error> Open() override;
  virtual awaitable<Error> Close() override;
  virtual awaitable<ErrorOr<any_transport>> Accept() override;
  virtual awaitable<ErrorOr<size_t>> Read(std::span<char> data) override;
  virtual awaitable<ErrorOr<size_t>> Write(std::span<const char> data) override;

 private:
  UdpSocketContext MakeUdpSocketImplContext();

  void OnSocketMessage(const UdpSocket::Endpoint& endpoint,
                       UdpSocket::Datagram&& datagram);
  void OnSocketClosed(const UdpSocket::Error& error);

  Executor executor_;
  log_source log_;
  std::shared_ptr<UdpPassiveCore> passive_core_;
  UdpSocket::Endpoint endpoint_;

  bool connected_ = true;

  boost::asio::experimental::channel<void(boost::system::error_code,
                                          std::vector<char> data)>
      received_message_channel_{
          executor_,
          /*max_buffer_size=*/std::numeric_limits<size_t>::max()};

  friend class UdpPassiveCore;
};

// AsioUdpTransport::UdpPassiveCore

class AsioUdpTransport::UdpPassiveCore final
    : public Core,
      public std::enable_shared_from_this<UdpPassiveCore> {
 public:
  UdpPassiveCore(const Executor& executor,
                 const log_source& log,
                 UdpSocketFactory udp_socket_factory,
                 std::string host,
                 std::string service);
  ~UdpPassiveCore();

  // Core
  virtual Executor get_executor() override { return executor_; }
  virtual bool connected() const override { return connected_; }
  virtual awaitable<Error> Open() override;
  virtual awaitable<Error> Close() override;
  virtual awaitable<ErrorOr<any_transport>> Accept() override;
  virtual awaitable<ErrorOr<size_t>> Read(std::span<char> data) override;
  virtual awaitable<ErrorOr<size_t>> Write(std::span<const char> data) override;

 private:
  UdpSocketContext MakeUdpSocketImplContext();

  void OnSocketOpened(const UdpSocket::Endpoint& endpoint);
  void OnSocketMessage(const UdpSocket::Endpoint& endpoint,
                       UdpSocket::Datagram&& datagram);
  void OnSocketClosed(const UdpSocket::Error& error);

  [[nodiscard]] awaitable<ErrorOr<size_t>> InternalWrite(
      UdpSocket::Endpoint endpoint,
      std::span<const char> datagram);

  void RemoveAcceptedTransport(const UdpSocket::Endpoint& endpoint);
  void CloseAllAcceptedTransports(Error error);

  const Executor executor_;
  const log_source log_;
  const UdpSocketFactory udp_socket_factory_;
  const std::string host_;
  const std::string service_;

  std::shared_ptr<UdpSocket> socket_;

  bool connected_ = false;

  std::map<UdpSocket::Endpoint, std::shared_ptr<UdpAcceptedCore>>
      accepted_transports_;

  boost::asio::experimental::channel<void(boost::system::error_code,
                                          std::shared_ptr<UdpAcceptedCore>)>
      accept_channel_{executor_,
                      /*max_buffer_size=*/std::numeric_limits<size_t>::max()};

  friend class UdpAcceptedCore;
};

// AsioUdpTransport::UdpPassiveCore

AsioUdpTransport::UdpPassiveCore::UdpPassiveCore(
    const Executor& executor,
    const log_source& log,
    UdpSocketFactory udp_socket_factory,
    std::string host,
    std::string service)
    : executor_{executor},
      log_{log},
      udp_socket_factory_{std::move(udp_socket_factory)},
      host_{std::move(host)},
      service_{std::move(service)} {}

AsioUdpTransport::UdpPassiveCore::~UdpPassiveCore() {
  assert(accepted_transports_.empty());
}

awaitable<Error> AsioUdpTransport::UdpPassiveCore::Open() {
  log_.writef(LogSeverity::Normal, "Open");

  socket_ = udp_socket_factory_(MakeUdpSocketImplContext());

  return socket_->Open();
}

awaitable<Error> AsioUdpTransport::UdpPassiveCore::Close() {
  auto ref = shared_from_this();

  co_await boost::asio::dispatch(executor_, boost::asio::use_awaitable);

  log_.write(LogSeverity::Normal, "Close");

  connected_ = false;

  co_await socket_->Close();

  CloseAllAcceptedTransports(OK);

  co_return OK;
}

awaitable<ErrorOr<any_transport>> AsioUdpTransport::UdpPassiveCore::Accept() {
  auto ref = shared_from_this();

  auto [ec, accepted_transport] = co_await accept_channel_.async_receive(
      boost::asio::experimental::as_tuple(boost::asio::use_awaitable));

  if (ec) {
    co_return ec;
  }

  co_return any_transport{
      std::make_unique<AsioUdpTransport>(accepted_transport)};
}

awaitable<ErrorOr<size_t>> AsioUdpTransport::UdpPassiveCore::Read(
    std::span<char> data) {
  co_return ERR_FAILED;
}

awaitable<ErrorOr<size_t>> AsioUdpTransport::UdpPassiveCore::Write(
    std::span<const char> data) {
  assert(false);
  co_return ERR_FAILED;
}

awaitable<ErrorOr<size_t>> AsioUdpTransport::UdpPassiveCore::InternalWrite(
    UdpSocket::Endpoint endpoint,
    std::span<const char> datagram) {
  return socket_->SendTo(std::move(endpoint), datagram);
}

void AsioUdpTransport::UdpPassiveCore::RemoveAcceptedTransport(
    const UdpSocket::Endpoint& endpoint) {
  boost::asio::dispatch(executor_, [this, endpoint, ref = shared_from_this()] {
    log_.writef(
        LogSeverity::Normal,
        "Remove transport from endpoint %s. There are %d accepted transports",
        ToString(endpoint).c_str(),
        static_cast<int>(accepted_transports_.size()));

    assert(accepted_transports_.contains(endpoint));

    accepted_transports_.erase(endpoint);
  });
}

void AsioUdpTransport::UdpPassiveCore::OnSocketMessage(
    const UdpSocket::Endpoint& endpoint,
    UdpSocket::Datagram&& datagram) {
  if (auto i = accepted_transports_.find(endpoint);
      i != accepted_transports_.end()) {
    i->second->OnSocketMessage(endpoint, std::move(datagram));
    return;
  }

  log_.writef(
      LogSeverity::Normal,
      "Accept new transport from endpoint %s. There are %d accepted transports",
      ToString(endpoint).c_str(),
      static_cast<int>(accepted_transports_.size()));

  auto accepted_core = std::make_shared<UdpAcceptedCore>(
      executor_, log_, shared_from_this(), endpoint);

  accepted_transports_.insert_or_assign(endpoint, accepted_core);

  bool posted =
      accept_channel_.try_send(boost::system::error_code{}, accepted_core);

  if (!posted) {
    log_.write(LogSeverity::Error, "Accept queue is full");
    return;
  }

  accepted_core->OnSocketMessage(endpoint, std::move(datagram));
}

void AsioUdpTransport::UdpPassiveCore::CloseAllAcceptedTransports(Error error) {
  log_.writef(LogSeverity::Normal, "Close %d accepted transports - %s",
              static_cast<int>(accepted_transports_.size()),
              ErrorToString(error).c_str());

  std::vector<std::shared_ptr<UdpAcceptedCore>> accepted_transports;
  accepted_transports.reserve(accepted_transports_.size());
  std::ranges::copy(accepted_transports_ | std::views::values,
                    std::back_inserter(accepted_transports));

  for (const auto& accepted_transport : accepted_transports) {
    accepted_transport->OnSocketClosed(UdpSocket::Error{});
  }
}

void AsioUdpTransport::UdpPassiveCore::OnSocketOpened(
    const UdpSocket::Endpoint& endpoint) {
  log_.writef(LogSeverity::Normal, "Opened with endpoint %s",
              ToString(endpoint).c_str());

  connected_ = true;
}

void AsioUdpTransport::UdpPassiveCore::OnSocketClosed(
    const UdpSocket::Error& error) {
  log_.writef(LogSeverity::Normal, "Closed - %s", error.message().c_str());

  connected_ = false;
  CloseAllAcceptedTransports(error);
}

UdpSocketContext AsioUdpTransport::UdpPassiveCore::MakeUdpSocketImplContext() {
  return {
      executor_,
      host_,
      service_,
      false,
      [weak_ptr = weak_from_this()](const UdpSocket::Endpoint& endpoint) {
        if (auto ref = weak_ptr.lock())
          ref->OnSocketOpened(endpoint);
      },
      [weak_ptr = weak_from_this()](const UdpSocket::Endpoint& endpoint,
                                    UdpSocket::Datagram&& datagram) {
        if (auto ref = weak_ptr.lock())
          ref->OnSocketMessage(endpoint, std::move(datagram));
      },
      [weak_ptr = weak_from_this()](const UdpSocket::Error& error) {
        if (auto ref = weak_ptr.lock())
          ref->OnSocketClosed(error);
      },
  };
}

// AsioUdpTransport::UdpAcceptedCore

AsioUdpTransport::UdpAcceptedCore::UdpAcceptedCore(
    const Executor& executor,
    const log_source& log,
    std::shared_ptr<UdpPassiveCore> passive_core,
    UdpSocket::Endpoint endpoint)
    : executor_{executor},
      log_{log},
      passive_core_{std::move(passive_core)},
      endpoint_{std::move(endpoint)} {
  assert(passive_core_);
}

AsioUdpTransport::UdpAcceptedCore::~UdpAcceptedCore() {
  if (passive_core_) {
    passive_core_->RemoveAcceptedTransport(endpoint_);
  }
}

awaitable<Error> AsioUdpTransport::UdpAcceptedCore::Open() {
  co_return ERR_ADDRESS_IN_USE;
}

awaitable<Error> AsioUdpTransport::UdpAcceptedCore::Close() {
  if (passive_core_) {
    passive_core_->RemoveAcceptedTransport(endpoint_);
    passive_core_ = nullptr;
  }

  connected_ = false;

  co_return OK;
}

awaitable<ErrorOr<any_transport>> AsioUdpTransport::UdpAcceptedCore::Accept() {
  co_return ERR_FAILED;
}

awaitable<ErrorOr<size_t>> AsioUdpTransport::UdpAcceptedCore::Read(
    std::span<char> data) {
  auto [ec, message] = co_await received_message_channel_.async_receive(
      boost::asio::experimental::as_tuple(boost::asio::use_awaitable));

  if (ec) {
    co_return ec;
  }

  if (data.size() < message.size()) {
    co_return ERR_INVALID_ARGUMENT;
  }

  std::ranges::copy(message, data.begin());
  co_return message.size();
}

awaitable<ErrorOr<size_t>> AsioUdpTransport::UdpAcceptedCore::Write(
    std::span<const char> data) {
  if (!passive_core_ || !connected_) {
    co_return ERR_CONNECTION_CLOSED;
  }

  co_return co_await passive_core_->InternalWrite(endpoint_, data);
}

void AsioUdpTransport::UdpAcceptedCore::OnSocketMessage(
    const UdpSocket::Endpoint& endpoint,
    UdpSocket::Datagram&& datagram) {
  bool posted = received_message_channel_.try_send(boost::system::error_code{},
                                                   std::move(datagram));

  if (!posted) {
    log_.write(LogSeverity::Error, "Received message queue is full");
    return;
  }
}

void AsioUdpTransport::UdpAcceptedCore::OnSocketClosed(
    const UdpSocket::Error& error) {
  assert(passive_core_);

  if (passive_core_) {
    passive_core_->RemoveAcceptedTransport(endpoint_);
    passive_core_ = nullptr;
  }

  connected_ = false;
}

// AsioUdpTransport

AsioUdpTransport::AsioUdpTransport(const Executor& executor,
                                   const log_source& log,
                                   UdpSocketFactory udp_socket_factory,
                                   std::string host,
                                   std::string service,
                                   bool active)
    : active_{active} {
  if (active_) {
    core_ =
        std::make_shared<UdpActiveCore>(executor, std::move(udp_socket_factory),
                                        std::move(host), std::move(service));
  } else {
    core_ = std::make_shared<UdpPassiveCore>(
        executor, log, std::move(udp_socket_factory), std::move(host),
        std::move(service));
  }
}

AsioUdpTransport::AsioUdpTransport(std::shared_ptr<Core> core)
    : active_{false} {
  core_ = std::move(core);
}

awaitable<Error> AsioUdpTransport::open() {
  return core_->Open();
}

std::string AsioUdpTransport::name() const {
  return "UDP";
}

awaitable<ErrorOr<any_transport>> AsioUdpTransport::accept() {
  NET_ASSIGN_OR_CO_RETURN(auto accepted_transport, co_await core_->Accept());

  co_return any_transport{std::move(accepted_transport)};
}

}  // namespace transport
