#include "net/udp_transport.h"

#include <map>

#include "net/logger.h"
#include "net/transport_util.h"
#include "net/udp_socket_impl.h"

std::string ToString(const net::UdpSocket::Endpoint& endpoint) {
  std::stringstream stream;
  stream << endpoint;
  return stream.str();
}

namespace net {

// AsioUdpTransport::UdpActiveCore

class AsioUdpTransport::UdpActiveCore
    : public Core,
      public std::enable_shared_from_this<UdpActiveCore> {
 public:
  UdpActiveCore(UdpSocketFactory udp_socket_factory,
                std::string host,
                std::string service);

  // Core
  virtual bool IsConnected() const override { return connected_; }
  virtual void Open(const Handlers& handlers) override;
  virtual void Close() override;
  virtual int Read(std::span<char> data) override;
  virtual promise<size_t> Write(std::span<const char> data) override;

 private:
  UdpSocketContext MakeUdpSocketImplContext();

  void OnSocketOpened(const UdpSocket::Endpoint& endpoint);
  void OnSocketMessage(const UdpSocket::Endpoint& endpoint,
                       UdpSocket::Datagram&& datagram);
  void OnSocketClosed(const UdpSocket::Error& error);

  const UdpSocketFactory udp_socket_factory_;
  const std::string host_;
  const std::string service_;

  std::shared_ptr<UdpSocket> socket_;

  Handlers handlers_;

  bool connected_ = false;
  UdpSocket::Endpoint peer_endpoint_;
};

AsioUdpTransport::UdpActiveCore::UdpActiveCore(
    UdpSocketFactory udp_socket_factory,
    std::string host,
    std::string service)
    : udp_socket_factory_{std::move(udp_socket_factory)},
      host_{std::move(host)},
      service_{std::move(service)} {}

void AsioUdpTransport::UdpActiveCore::Open(const Handlers& handlers) {
  handlers_ = handlers;

  socket_ = udp_socket_factory_(MakeUdpSocketImplContext());
  socket_->Open();
}

void AsioUdpTransport::UdpActiveCore::Close() {
  connected_ = false;
  socket_->Close();
}

int AsioUdpTransport::UdpActiveCore::Read(std::span<char> data) {
  assert(false);

  return net::ERR_FAILED;
}

promise<size_t> AsioUdpTransport::UdpActiveCore::Write(
    std::span<const char> data) {
  std::vector<char> datagram(data.begin(), data.end());
  return socket_->SendTo(peer_endpoint_, std::move(datagram));
}

void AsioUdpTransport::UdpActiveCore::OnSocketOpened(
    const UdpSocket::Endpoint& endpoint) {
  peer_endpoint_ = endpoint;
  connected_ = true;

  if (handlers_.on_open) {
    handlers_.on_open();
  }
}

void AsioUdpTransport::UdpActiveCore::OnSocketMessage(
    const UdpSocket::Endpoint& endpoint,
    UdpSocket::Datagram&& datagram) {
  if (handlers_.on_message) {
    handlers_.on_message(datagram);
  }
}

void AsioUdpTransport::UdpActiveCore::OnSocketClosed(
    const UdpSocket::Error& error) {
  connected_ = false;

  if (handlers_.on_close) {
    handlers_.on_close(net::MapSystemError(error.value()));
  }
}

UdpSocketContext AsioUdpTransport::UdpActiveCore::MakeUdpSocketImplContext() {
  return {
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
  virtual promise<void> Open(const Handlers& handlers) override;
  virtual void Close() override;
  virtual int Read(std::span<char> data) override;
  virtual promise<size_t> Write(std::span<const char> data) override;
  virtual std::string GetName() const override;
  virtual bool IsMessageOriented() const override;
  virtual bool IsConnected() const override;
  virtual bool IsActive() const override;

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
  UdpPassiveCore(std::shared_ptr<const Logger> logger,
                 UdpSocketFactory udp_socket_factory,
                 std::string host,
                 std::string service);
  ~UdpPassiveCore();

  // Core
  virtual bool IsConnected() const override { return connected_; }
  virtual void Open(const Handlers& handlers) override;
  virtual void Close() override;
  virtual int Read(std::span<char> data) override;
  virtual promise<size_t> Write(std::span<const char> data) override;

 private:
  UdpSocketContext MakeUdpSocketImplContext();

  void OnSocketOpened(const UdpSocket::Endpoint& endpoint);
  void OnSocketMessage(const UdpSocket::Endpoint& endpoint,
                       UdpSocket::Datagram&& datagram);
  void OnSocketClosed(const UdpSocket::Error& error);

  promise<size_t> InternalWrite(const UdpSocket::Endpoint& endpoint,
                                UdpSocket::Datagram&& datagram);
  void RemoveAcceptedTransport(const UdpSocket::Endpoint& endpoint);
  void CloseAllAcceptedTransports(Error error);

  const std::shared_ptr<const Logger> logger_;
  const UdpSocketFactory udp_socket_factory_;
  const std::string host_;
  const std::string service_;

  std::shared_ptr<UdpSocket> socket_;

  Handlers handlers_;

  bool connected_ = false;

  std::map<UdpSocket::Endpoint, AcceptedTransport*> accepted_transports_;

  friend class AcceptedTransport;
};

// AsioUdpTransport::UdpPassiveCore

AsioUdpTransport::UdpPassiveCore::UdpPassiveCore(
    std::shared_ptr<const Logger> logger,
    UdpSocketFactory udp_socket_factory,
    std::string host,
    std::string service)
    : logger_{std::make_shared<ProxyLogger>(std::move(logger))},
      udp_socket_factory_{std::move(udp_socket_factory)},
      host_{std::move(host)},
      service_{std::move(service)} {}

AsioUdpTransport::UdpPassiveCore::~UdpPassiveCore() {
  assert(accepted_transports_.empty());
}

void AsioUdpTransport::UdpPassiveCore::Open(const Handlers& handlers) {
  logger_->WriteF(LogSeverity::Normal, "Open");

  handlers_ = handlers;

  socket_ = udp_socket_factory_(MakeUdpSocketImplContext());
  socket_->Open();
}

void AsioUdpTransport::UdpPassiveCore::Close() {
  logger_->Write(LogSeverity::Normal, "Close");

  connected_ = false;

  socket_->Close();

  CloseAllAcceptedTransports(OK);
}

int AsioUdpTransport::UdpPassiveCore::Read(std::span<char> data) {
  assert(false);

  return ERR_FAILED;
}

promise<size_t> AsioUdpTransport::UdpPassiveCore::Write(
    std::span<const char> data) {
  assert(false);

  return make_error_promise<size_t>(ERR_FAILED);
}

promise<size_t> AsioUdpTransport::UdpPassiveCore::InternalWrite(
    const UdpSocket::Endpoint& endpoint,
    UdpSocket::Datagram&& datagram) {
  return socket_->SendTo(endpoint, std::move(datagram));
}

void AsioUdpTransport::UdpPassiveCore::RemoveAcceptedTransport(
    const UdpSocket::Endpoint& endpoint) {
  logger_->WriteF(LogSeverity::Normal, "Remove transport from endpoint %s",
                  ToString(endpoint).c_str());

  assert(accepted_transports_.find(endpoint) != accepted_transports_.end());

  accepted_transports_.erase(endpoint);
}

void AsioUdpTransport::UdpPassiveCore::OnSocketMessage(
    const UdpSocket::Endpoint& endpoint,
    UdpSocket::Datagram&& datagram) {
  {
    auto i = accepted_transports_.find(endpoint);
    if (i != accepted_transports_.end()) {
      i->second->ProcessDatagram(std::move(datagram));
      return;
    }
  }

  logger_->WriteF(LogSeverity::Normal, "Accept new transport from endpoint %s",
                  ToString(endpoint).c_str());

  if (handlers_.on_accept) {
    auto accepted_transport =
        std::make_unique<AcceptedTransport>(shared_from_this(), endpoint);
    accepted_transports_.insert_or_assign(endpoint, accepted_transport.get());
    handlers_.on_accept(std::move(accepted_transport));
  }

  // Accepted transport can be deleted from the callback above.
  {
    auto i = accepted_transports_.find(endpoint);
    if (i != accepted_transports_.end())
      i->second->ProcessDatagram(std::move(datagram));
  }
}

void AsioUdpTransport::UdpPassiveCore::CloseAllAcceptedTransports(Error error) {
  logger_->WriteF(LogSeverity::Normal, "Close %d accepted transports - %s",
                  static_cast<int>(accepted_transports_.size()),
                  ErrorToString(error).c_str());

  std::vector<AcceptedTransport*> accepted_transports;
  accepted_transports.reserve(accepted_transports_.size());
  std::transform(accepted_transports_.begin(), accepted_transports_.end(),
                 std::back_inserter(accepted_transports),
                 [](auto& p) { return p.second; });

  for (auto* accepted_transport : accepted_transports)
    accepted_transport->ProcessError(error);
}

void AsioUdpTransport::UdpPassiveCore::OnSocketOpened(
    const UdpSocket::Endpoint& endpoint) {
  logger_->WriteF(LogSeverity::Normal, "Opened with endpoint %s",
                  ToString(endpoint).c_str());

  connected_ = true;

  if (handlers_.on_open) {
    handlers_.on_open();
  }
}

void AsioUdpTransport::UdpPassiveCore::OnSocketClosed(
    const UdpSocket::Error& error) {
  logger_->WriteF(LogSeverity::Normal, "Closed - %s", error.message().c_str());

  auto net_error = net::MapSystemError(error.value());
  connected_ = false;
  CloseAllAcceptedTransports(net_error);

  if (handlers_.on_close) {
    handlers_.on_close(net_error);
  }
}

UdpSocketContext AsioUdpTransport::UdpPassiveCore::MakeUdpSocketImplContext() {
  return {
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

promise<void> AsioUdpTransport::AcceptedTransport::Open(
    const Handlers& handlers) {
  assert(!connected_);

  if (connected_ || !core_) {
    return make_resolved_promise();
  }

  auto [p, promise_handlers] = MakePromiseHandlers(handlers);

  connected_ = true;
  handlers_ = std::move(promise_handlers);

  return p;
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

int AsioUdpTransport::AcceptedTransport::Read(std::span<char> data) {
  assert(false);

  return ERR_FAILED;
}

promise<size_t> AsioUdpTransport::AcceptedTransport::Write(
    std::span<const char> data) {
  assert(connected_);

  if (!core_ || !connected_)
    return make_error_promise<size_t>(ERR_FAILED);

  std::vector<char> datagram(data.begin(), data.end());
  return core_->InternalWrite(endpoint_, std::move(datagram));
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

void AsioUdpTransport::AcceptedTransport::ProcessDatagram(Datagram&& datagram) {
  assert(core_);

  if (connected_ && handlers_.on_message)
    handlers_.on_message(datagram);
}

void AsioUdpTransport::AcceptedTransport::ProcessError(Error error) {
  assert(core_);

  if (core_) {
    core_->RemoveAcceptedTransport(endpoint_);
    core_ = nullptr;
  }

  if (connected_) {
    connected_ = false;

    if (handlers_.on_close)
      handlers_.on_close(error);
  }
}

// AsioUdpTransport

AsioUdpTransport::AsioUdpTransport(std::shared_ptr<const Logger> logger,
                                   UdpSocketFactory udp_socket_factory,
                                   std::string host,
                                   std::string service,
                                   bool active)
    : logger_{std::move(logger)},
      udp_socket_factory_{std::move(udp_socket_factory)},
      host_{std::move(host)},
      service_{std::move(service)},
      active_{active} {}

promise<void> AsioUdpTransport::Open(const Handlers& handlers) {
  if (core_) {
    core_->Close();
  }

  if (!core_) {
    core_ =
        active_
            ? std::static_pointer_cast<Core>(std::make_shared<UdpActiveCore>(
                  udp_socket_factory_, host_, service_))
            : std::static_pointer_cast<Core>(std::make_shared<UdpPassiveCore>(
                  logger_, udp_socket_factory_, host_, service_));
  }

  auto [p, promise_handlers] = MakePromiseHandlers(handlers);

  core_->Open(promise_handlers);

  return p;
}

std::string AsioUdpTransport::GetName() const {
  return "UDP";
}

}  // namespace net
