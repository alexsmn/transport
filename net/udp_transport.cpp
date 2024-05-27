#include "net/udp_transport.h"

#include "base/threading/thread_collision_warner.h"
#include "net/logger.h"
#include "net/udp_socket_impl.h"

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

  [[nodiscard]] virtual awaitable<Error> Open(Handlers handlers) override;

  virtual void Close() override;

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

  DFAKE_MUTEX(mutex_);

  std::shared_ptr<UdpSocket> socket_;

  Handlers handlers_;

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

awaitable<Error> AsioUdpTransport::UdpActiveCore::Open(Handlers handlers) {
  DFAKE_SCOPED_RECURSIVE_LOCK(mutex_);

  handlers_ = std::move(handlers);
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

// AsioUdpTransport::AcceptedTransport

class NET_EXPORT AsioUdpTransport::AcceptedTransport final : public Transport {
 public:
  using Socket = boost::asio::ip::udp::socket;
  using Endpoint = Socket::endpoint_type;
  using Datagram = std::vector<char>;

  AcceptedTransport(std::shared_ptr<UdpPassiveCore> core, Endpoint endpoint);
  ~AcceptedTransport();

  // Transport
  virtual awaitable<Error> Open(Handlers handlers) override;
  virtual void Close() override;
  virtual awaitable<ErrorOr<size_t>> Read(std::span<char> data) override;
  virtual awaitable<ErrorOr<size_t>> Write(std::span<const char> data) override;
  virtual std::string GetName() const override;
  virtual bool IsMessageOriented() const override;
  virtual bool IsConnected() const override;
  virtual bool IsActive() const override;
  virtual Executor GetExecutor() const override;

  std::string host;
  std::string service;

 private:
  void ProcessDatagram(Datagram&& datagram);
  void ProcessError(Error error);

  std::shared_ptr<UdpPassiveCore> core_;
  const Endpoint endpoint_;

  bool connected_ = false;

  Handlers handlers_;

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
  virtual awaitable<Error> Open(Handlers handlers) override;
  virtual void Close() override;
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

  DFAKE_MUTEX(mutex_);

  std::shared_ptr<UdpSocket> socket_;

  Handlers handlers_;

  bool connected_ = false;

  std::map<UdpSocket::Endpoint, AcceptedTransport*> accepted_transports_;

  friend class AcceptedTransport;
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

awaitable<Error> AsioUdpTransport::UdpPassiveCore::Open(Handlers handlers) {
  logger_->WriteF(LogSeverity::Normal, "Open");

  handlers_ = std::move(handlers);
  socket_ = udp_socket_factory_(MakeUdpSocketImplContext());

  return socket_->Open();
}

void AsioUdpTransport::UdpPassiveCore::Close() {
  boost::asio::dispatch(executor_, [this, ref = shared_from_this()] {
    DFAKE_SCOPED_RECURSIVE_LOCK(mutex_);

    logger_->Write(LogSeverity::Normal, "Close");

    connected_ = false;

    boost::asio::co_spawn(executor_,
                          std::bind_front(&UdpSocket::Close, socket_),
                          boost::asio::detached);

    CloseAllAcceptedTransports(OK);
  });
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
    DFAKE_SCOPED_RECURSIVE_LOCK(mutex_);

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
  DFAKE_SCOPED_RECURSIVE_LOCK(mutex_);

  if (auto i = accepted_transports_.find(endpoint);
      i != accepted_transports_.end()) {
    i->second->ProcessDatagram(std::move(datagram));
    return;
  }

  logger_->WriteF(
      LogSeverity::Normal,
      "Accept new transport from endpoint %s. There are %d accepted transports",
      ToString(endpoint).c_str(),
      static_cast<int>(accepted_transports_.size()));

  if (handlers_.on_accept) {
    auto accepted_transport =
        std::make_unique<AcceptedTransport>(shared_from_this(), endpoint);
    accepted_transports_.insert_or_assign(endpoint, accepted_transport.get());
    handlers_.on_accept(std::move(accepted_transport));
  }

  // Accepted transport can be deleted from the callback above.
  if (auto i = accepted_transports_.find(endpoint);
      i != accepted_transports_.end()) {
    i->second->ProcessDatagram(std::move(datagram));
  }
}

void AsioUdpTransport::UdpPassiveCore::CloseAllAcceptedTransports(Error error) {
  DFAKE_SCOPED_RECURSIVE_LOCK(mutex_);

  logger_->WriteF(LogSeverity::Normal, "Close %d accepted transports - %s",
                  static_cast<int>(accepted_transports_.size()),
                  ErrorToString(error).c_str());

  std::vector<AcceptedTransport*> accepted_transports;
  accepted_transports.reserve(accepted_transports_.size());
  std::ranges::copy(accepted_transports_ | std::views::values,
                    std::back_inserter(accepted_transports));

  for (auto* accepted_transport : accepted_transports) {
    accepted_transport->ProcessError(error);
  }
}

void AsioUdpTransport::UdpPassiveCore::OnSocketOpened(
    const UdpSocket::Endpoint& endpoint) {
  DFAKE_SCOPED_RECURSIVE_LOCK(mutex_);

  logger_->WriteF(LogSeverity::Normal, "Opened with endpoint %s",
                  ToString(endpoint).c_str());

  connected_ = true;
}

void AsioUdpTransport::UdpPassiveCore::OnSocketClosed(
    const UdpSocket::Error& error) {
  DFAKE_SCOPED_RECURSIVE_LOCK(mutex_);

  logger_->WriteF(LogSeverity::Normal, "Closed - %s", error.message().c_str());

  auto net_error = net::MapSystemError(error.value());
  connected_ = false;
  CloseAllAcceptedTransports(net_error);
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

// AsioUdpTransport::AcceptedTransport

AsioUdpTransport::AcceptedTransport::AcceptedTransport(
    std::shared_ptr<UdpPassiveCore> core,
    Endpoint endpoint)
    : core_{std::move(core)}, endpoint_{std::move(endpoint)} {
  assert(core_);
}

AsioUdpTransport::AcceptedTransport::~AcceptedTransport() {
  if (core_)
    core_->RemoveAcceptedTransport(endpoint_);
}

awaitable<Error> AsioUdpTransport::AcceptedTransport::Open(Handlers handlers) {
  assert(!connected_);

  if (connected_ || !core_) {
    co_return ERR_ADDRESS_IN_USE;
  }

  connected_ = true;
  handlers_ = std::move(handlers);

  co_return OK;
}

void AsioUdpTransport::AcceptedTransport::Close() {
  assert(connected_);

  if (core_) {
    core_->RemoveAcceptedTransport(endpoint_);
    core_ = nullptr;
  }

  handlers_ = {};
  connected_ = false;
}

awaitable<ErrorOr<size_t>> AsioUdpTransport::AcceptedTransport::Read(
    std::span<char> data) {
  co_return ERR_FAILED;
}

awaitable<ErrorOr<size_t>> AsioUdpTransport::AcceptedTransport::Write(
    std::span<const char> data) {
  if (!core_ || !connected_) {
    co_return ERR_CONNECTION_CLOSED;
  }

  co_return co_await core_->InternalWrite(endpoint_, data);
}

std::string AsioUdpTransport::AcceptedTransport::GetName() const {
  return "UDP";
}

bool AsioUdpTransport::AcceptedTransport::IsMessageOriented() const {
  return true;
}

bool AsioUdpTransport::AcceptedTransport::IsConnected() const {
  return connected_;
}

bool AsioUdpTransport::AcceptedTransport::IsActive() const {
  return false;
}

Executor AsioUdpTransport::AcceptedTransport::GetExecutor() const {
  return core_->GetExecutor();
}

void AsioUdpTransport::AcceptedTransport::ProcessDatagram(Datagram&& datagram) {
  assert(core_);

  /* if (connected_ && handlers_.on_message)
    handlers_.on_message(datagram);*/
}

void AsioUdpTransport::AcceptedTransport::ProcessError(Error error) {
  assert(core_);

  if (core_) {
    core_->RemoveAcceptedTransport(endpoint_);
    core_ = nullptr;
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

awaitable<Error> AsioUdpTransport::Open(Handlers handlers) {
  return core_->Open(std::move(handlers));
}

std::string AsioUdpTransport::GetName() const {
  return "UDP";
}

}  // namespace net
