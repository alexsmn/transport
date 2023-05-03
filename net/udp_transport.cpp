#include "udp_transport.h"

#include "logger.h"
#include "udp_socket_impl.h"

#include <map>

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
  virtual void Open(Delegate& delegate) override;
  virtual void Close() override;
  virtual int Read(void* data, size_t len) override;
  virtual int Write(const void* data, size_t len) override;

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

  Delegate* delegate_ = nullptr;

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

void AsioUdpTransport::UdpActiveCore::Open(Delegate& delegate) {
  delegate_ = &delegate;

  socket_ = udp_socket_factory_(MakeUdpSocketImplContext());
  socket_->Open();
}

void AsioUdpTransport::UdpActiveCore::Close() {
  connected_ = false;
  socket_->Close();
}

int AsioUdpTransport::UdpActiveCore::Read(void* data, size_t len) {
  assert(false);

  return net::ERR_FAILED;
}

int AsioUdpTransport::UdpActiveCore::Write(const void* data, size_t len) {
  std::vector<char> datagram(len);
  memcpy(datagram.data(), data, len);

  socket_->SendTo(peer_endpoint_, std::move(datagram));

  return static_cast<int>(len);
}

void AsioUdpTransport::UdpActiveCore::OnSocketOpened(
    const UdpSocket::Endpoint& endpoint) {
  peer_endpoint_ = endpoint;
  connected_ = true;
  delegate_->OnTransportOpened();
}

void AsioUdpTransport::UdpActiveCore::OnSocketMessage(
    const UdpSocket::Endpoint& endpoint,
    UdpSocket::Datagram&& datagram) {
  delegate_->OnTransportMessageReceived(datagram.data(), datagram.size());
}

void AsioUdpTransport::UdpActiveCore::OnSocketClosed(
    const UdpSocket::Error& error) {
  connected_ = false;
  delegate_->OnTransportClosed(net::MapSystemError(error.value()));
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
  virtual Error Open(Delegate& delegate) override;
  virtual void Close() override;
  virtual int Read(void* data, size_t len) override;
  virtual int Write(const void* data, size_t len) override;
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

  Delegate* delegate_ = nullptr;

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
  virtual void Open(Delegate& delegate) override;
  virtual void Close() override;
  virtual int Read(void* data, size_t len) override;
  virtual int Write(const void* data, size_t len) override;

 private:
  UdpSocketContext MakeUdpSocketImplContext();

  void OnSocketOpened(const UdpSocket::Endpoint& endpoint);
  void OnSocketMessage(const UdpSocket::Endpoint& endpoint,
                       UdpSocket::Datagram&& datagram);
  void OnSocketClosed(const UdpSocket::Error& error);

  void InternalWrite(const UdpSocket::Endpoint& endpoint,
                     UdpSocket::Datagram&& datagram);
  void RemoveAcceptedTransport(const UdpSocket::Endpoint& endpoint);
  void CloseAllAcceptedTransports(Error error);

  const std::shared_ptr<const Logger> logger_;
  const UdpSocketFactory udp_socket_factory_;
  const std::string host_;
  const std::string service_;

  std::shared_ptr<UdpSocket> socket_;

  Delegate* delegate_ = nullptr;

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

void AsioUdpTransport::UdpPassiveCore::Open(Delegate& delegate) {
  logger_->WriteF(LogSeverity::Normal, "Open");

  delegate_ = &delegate;

  socket_ = udp_socket_factory_(MakeUdpSocketImplContext());
  socket_->Open();
}

void AsioUdpTransport::UdpPassiveCore::Close() {
  logger_->Write(LogSeverity::Normal, "Close");

  connected_ = false;

  socket_->Close();

  CloseAllAcceptedTransports(OK);
}

int AsioUdpTransport::UdpPassiveCore::Read(void* data, size_t len) {
  assert(false);

  return ERR_FAILED;
}

int AsioUdpTransport::UdpPassiveCore::Write(const void* data, size_t len) {
  assert(false);

  return ERR_FAILED;
}

void AsioUdpTransport::UdpPassiveCore::InternalWrite(
    const UdpSocket::Endpoint& endpoint,
    UdpSocket::Datagram&& datagram) {
  socket_->SendTo(endpoint, std::move(datagram));
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

  auto accepted_transport =
      std::make_unique<AcceptedTransport>(shared_from_this(), endpoint);
  accepted_transports_.emplace(endpoint, accepted_transport.get());
  delegate_->OnTransportAccepted(std::move(accepted_transport));

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
  delegate_->OnTransportOpened();
}

void AsioUdpTransport::UdpPassiveCore::OnSocketClosed(
    const UdpSocket::Error& error) {
  logger_->WriteF(LogSeverity::Normal, "Closed - %s", error.message().c_str());

  auto net_error = net::MapSystemError(error.value());
  connected_ = false;
  CloseAllAcceptedTransports(net_error);
  delegate_->OnTransportClosed(net_error);
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

Error AsioUdpTransport::AcceptedTransport::Open(Delegate& delegate) {
  assert(!connected_);

  if (connected_ || !core_)
    return ERR_FAILED;

  delegate_ = &delegate;
  connected_ = true;

  return OK;
}

void AsioUdpTransport::AcceptedTransport::Close() {
  assert(connected_);

  if (core_) {
    core_->RemoveAcceptedTransport(endpoint_);
    core_ = nullptr;
  }

  delegate_ = nullptr;
  connected_ = false;
}

int AsioUdpTransport::AcceptedTransport::Read(void* data, size_t len) {
  assert(false);

  return ERR_FAILED;
}

int AsioUdpTransport::AcceptedTransport::Write(const void* data, size_t len) {
  assert(connected_);

  if (!core_ || !connected_)
    return ERR_FAILED;

  std::vector<char> datagram(len);
  memcpy(datagram.data(), data, len);

  core_->InternalWrite(endpoint_, std::move(datagram));

  return static_cast<int>(len);
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

  if (connected_ && delegate_)
    delegate_->OnTransportMessageReceived(datagram.data(), datagram.size());
}

void AsioUdpTransport::AcceptedTransport::ProcessError(Error error) {
  assert(core_);

  if (core_) {
    core_->RemoveAcceptedTransport(endpoint_);
    core_ = nullptr;
  }

  if (connected_) {
    connected_ = false;

    if (delegate_)
      delegate_->OnTransportClosed(error);
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

Error AsioUdpTransport::Open(Transport::Delegate& delegate) {
  if (core_)
    core_->Close();

  if (active_) {
    core_ =
        std::make_shared<UdpActiveCore>(udp_socket_factory_, host_, service_);
  } else {
    core_ = std::make_shared<UdpPassiveCore>(logger_, udp_socket_factory_,
                                             host_, service_);
  }

  core_->Open(delegate);
  return net::OK;
}

std::string AsioUdpTransport::GetName() const {
  return "UDP";
}

}  // namespace net
