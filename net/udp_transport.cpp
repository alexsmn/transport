#include "net/udp_transport.h"

#include "net/logger.h"
#include "net/udp_socket_impl.h"

#include <boost/asio/experimental/as_tuple.hpp>
#include <boost/asio/experimental/channel.hpp>
#include <map>
#include <ranges>

std::string ToString(const net::UdpSocket::Endpoint& endpoint) {
  std::stringstream stream;
  stream << endpoint;
  return stream.str();
}

namespace net {

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
  virtual Executor GetExecutor() override { return executor_; }
  virtual bool IsConnected() const override { return connected_; }

  [[nodiscard]] virtual awaitable<Error> Open() override;

  virtual void Close() override;

  [[nodiscard]] virtual awaitable<ErrorOr<std::unique_ptr<Transport>>> Accept()
      override;

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

void AsioUdpTransport::UdpActiveCore::Close() {
  boost::asio::dispatch(executor_, [this, ref = shared_from_this()] {
    connected_ = false;

    boost::asio::co_spawn(executor_,
                          std::bind_front(&UdpSocket::Close, socket_),
                          boost::asio::detached);
  });
}

awaitable<ErrorOr<std::unique_ptr<Transport>>>
AsioUdpTransport::UdpActiveCore::Accept() {
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
                  std::shared_ptr<const Logger> logger,
                  std::shared_ptr<UdpPassiveCore> passive_core,
                  UdpSocket::Endpoint endpoint);
  ~UdpAcceptedCore();

  // Core
  virtual Executor GetExecutor() override { return executor_; }
  virtual bool IsConnected() const override { return connected_; }
  virtual awaitable<Error> Open() override;
  virtual void Close() override;
  virtual awaitable<ErrorOr<std::unique_ptr<Transport>>> Accept() override;
  virtual awaitable<ErrorOr<size_t>> Read(std::span<char> data) override;
  virtual awaitable<ErrorOr<size_t>> Write(std::span<const char> data) override;

 private:
  UdpSocketContext MakeUdpSocketImplContext();

  void OnSocketMessage(const UdpSocket::Endpoint& endpoint,
                       UdpSocket::Datagram&& datagram);
  void OnSocketClosed(const UdpSocket::Error& error);

  const Executor executor_;
  const std::shared_ptr<const Logger> logger_;
  std::shared_ptr<UdpPassiveCore> passive_core_;
  const UdpSocket::Endpoint endpoint_;

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
                 std::shared_ptr<const Logger> logger,
                 UdpSocketFactory udp_socket_factory,
                 std::string host,
                 std::string service);
  ~UdpPassiveCore();

  // Core
  virtual Executor GetExecutor() override { return executor_; }
  virtual bool IsConnected() const override { return connected_; }
  virtual awaitable<Error> Open() override;
  virtual void Close() override;
  virtual awaitable<ErrorOr<std::unique_ptr<Transport>>> Accept() override;
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
  const std::shared_ptr<const Logger> logger_;
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
    std::shared_ptr<const Logger> logger,
    UdpSocketFactory udp_socket_factory,
    std::string host,
    std::string service)
    : executor_{executor},
      logger_{std::make_shared<ProxyLogger>(std::move(logger))},
      udp_socket_factory_{std::move(udp_socket_factory)},
      host_{std::move(host)},
      service_{std::move(service)} {}

AsioUdpTransport::UdpPassiveCore::~UdpPassiveCore() {
  assert(accepted_transports_.empty());
}

awaitable<Error> AsioUdpTransport::UdpPassiveCore::Open() {
  logger_->WriteF(LogSeverity::Normal, "Open");

  socket_ = udp_socket_factory_(MakeUdpSocketImplContext());

  return socket_->Open();
}

void AsioUdpTransport::UdpPassiveCore::Close() {
  boost::asio::dispatch(executor_, [this, ref = shared_from_this()] {
    logger_->Write(LogSeverity::Normal, "Close");

    connected_ = false;

    boost::asio::co_spawn(executor_,
                          std::bind_front(&UdpSocket::Close, socket_),
                          boost::asio::detached);

    CloseAllAcceptedTransports(OK);
  });
}

awaitable<ErrorOr<std::unique_ptr<Transport>>>
AsioUdpTransport::UdpPassiveCore::Accept() {
  auto ref = shared_from_this();

  auto [ec, accepted_transport] = co_await accept_channel_.async_receive(
      boost::asio::experimental::as_tuple(boost::asio::use_awaitable));

  if (ec) {
    co_return ec;
  }

  co_return std::make_unique<AsioUdpTransport>(accepted_transport);
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
    logger_->WriteF(
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

  logger_->WriteF(
      LogSeverity::Normal,
      "Accept new transport from endpoint %s. There are %d accepted transports",
      ToString(endpoint).c_str(),
      static_cast<int>(accepted_transports_.size()));

  auto accepted_core = std::make_shared<UdpAcceptedCore>(
      executor_, logger_, shared_from_this(), endpoint);

  accepted_transports_.insert_or_assign(endpoint, accepted_core);

  bool posted =
      accept_channel_.try_send(boost::system::error_code{}, accepted_core);

  if (!posted) {
    logger_->Write(LogSeverity::Error, "Accept queue is full");
    return;
  }

  accepted_core->OnSocketMessage(endpoint, std::move(datagram));
}

void AsioUdpTransport::UdpPassiveCore::CloseAllAcceptedTransports(Error error) {
  logger_->WriteF(LogSeverity::Normal, "Close %d accepted transports - %s",
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
  logger_->WriteF(LogSeverity::Normal, "Opened with endpoint %s",
                  ToString(endpoint).c_str());

  connected_ = true;
}

void AsioUdpTransport::UdpPassiveCore::OnSocketClosed(
    const UdpSocket::Error& error) {
  logger_->WriteF(LogSeverity::Normal, "Closed - %s", error.message().c_str());

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
    std::shared_ptr<const Logger> logger,
    std::shared_ptr<UdpPassiveCore> passive_core,
    UdpSocket::Endpoint endpoint)
    : executor_{executor},
      logger_{std::move(logger)},
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

void AsioUdpTransport::UdpAcceptedCore::Close() {
  if (passive_core_) {
    passive_core_->RemoveAcceptedTransport(endpoint_);
    passive_core_ = nullptr;
  }

  connected_ = false;
}

awaitable<ErrorOr<std::unique_ptr<Transport>>>
AsioUdpTransport::UdpAcceptedCore::Accept() {
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
    logger_->Write(LogSeverity::Error, "Received message queue is full");
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
                                   std::shared_ptr<const Logger> logger,
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
        executor, std::move(logger), std::move(udp_socket_factory),
        std::move(host), std::move(service));
  }
}

AsioUdpTransport::AsioUdpTransport(std::shared_ptr<Core> core)
    : active_{false} {
  core_ = std::move(core);
}

awaitable<Error> AsioUdpTransport::Open() {
  return core_->Open();
}

std::string AsioUdpTransport::GetName() const {
  return "UDP";
}

awaitable<ErrorOr<std::unique_ptr<Transport>>> AsioUdpTransport::Accept() {
  return core_->Accept();
}

}  // namespace net
