#include "udp_transport.h"

#include "udp_socket_impl.h"

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

UdpSocketContext AsioUdpTransport::UdpActiveCore::MakeUdpSocketImplContext() {
  return {
      host_,
      service_,
      true,
      [this, weak_ptr = weak_from_this()](const UdpSocket::Endpoint& endpoint) {
        auto ref = weak_ptr.lock();
        if (!ref)
          return;
        peer_endpoint_ = endpoint;
        connected_ = true;
        delegate_->OnTransportOpened();
      },
      [this, weak_ptr = weak_from_this()](
          const UdpSocket::Endpoint& endpoint,
          const UdpSocket::Datagram&& datagram) {
        auto ref = weak_ptr.lock();
        if (!ref)
          return;
        delegate_->OnTransportMessageReceived(datagram.data(), datagram.size());
      },
      [this, weak_ptr = weak_from_this()](const UdpSocket::Error& error) {
        auto ref = weak_ptr.lock();
        if (!ref)
          return;
        connected_ = false;
        delegate_->OnTransportClosed(net::MapSystemError(error.value()));
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
  UdpPassiveCore(UdpSocketFactory udp_socket_factory,
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

  void InternalWrite(const UdpSocket::Endpoint& endpoint,
                     UdpSocket::Datagram&& datagram);
  void RemoveAcceptedTransport(const UdpSocket::Endpoint& endpoint);
  void CloseAllAcceptedTransports(Error error);
  void ProcessDatagram(const UdpSocket::Endpoint& endpoint,
                       UdpSocket::Datagram&& datagram);

  const UdpSocketFactory udp_socket_factory_;
  const std::string host_;
  const std::string service_;

  std::shared_ptr<UdpSocket> socket_;

  Delegate* delegate_ = nullptr;

  bool connected_ = false;

  std::map<UdpSocket::Endpoint, AcceptedTransport*> accepted_transports_;

  friend class AcceptedTransport;
};

AsioUdpTransport::UdpPassiveCore::UdpPassiveCore(
    UdpSocketFactory udp_socket_factory,
    std::string host,
    std::string service)
    : udp_socket_factory_{std::move(udp_socket_factory)},
      host_{std::move(host)},
      service_{std::move(service)} {}

AsioUdpTransport::UdpPassiveCore::~UdpPassiveCore() {
  assert(accepted_transports_.empty());
}

void AsioUdpTransport::UdpPassiveCore::Open(Delegate& delegate) {
  delegate_ = &delegate;

  socket_ = udp_socket_factory_(MakeUdpSocketImplContext());
  socket_->Open();
}

void AsioUdpTransport::UdpPassiveCore::Close() {
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
  assert(accepted_transports_.find(endpoint) != accepted_transports_.end());

  accepted_transports_.erase(endpoint);
}

void AsioUdpTransport::UdpPassiveCore::ProcessDatagram(
    const UdpSocket::Endpoint& endpoint,
    UdpSocket::Datagram&& datagram) {
  {
    auto i = accepted_transports_.find(endpoint);
    if (i != accepted_transports_.end()) {
      i->second->ProcessDatagram(std::move(datagram));
      return;
    }
  }

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
  std::vector<AcceptedTransport*> accepted_transports;
  accepted_transports.reserve(accepted_transports_.size());
  std::transform(accepted_transports_.begin(), accepted_transports_.end(),
                 std::back_inserter(accepted_transports),
                 [](auto& p) { return p.second; });
  for (auto* accepted_transport : accepted_transports)
    accepted_transport->ProcessError(error);
}

UdpSocketContext AsioUdpTransport::UdpPassiveCore::MakeUdpSocketImplContext() {
  return {
      host_,
      service_,
      false,
      [this, weak_ptr = weak_from_this()](const UdpSocket::Endpoint& endpoint) {
        auto ref = weak_ptr.lock();
        if (!ref)
          return;
        connected_ = true;
        delegate_->OnTransportOpened();
      },
      [this, weak_ptr = weak_from_this()](const UdpSocket::Endpoint& endpoint,
                                          UdpSocket::Datagram&& datagram) {
        auto ref = weak_ptr.lock();
        if (!ref)
          return;
        ProcessDatagram(endpoint, std::move(datagram));
      },
      [this, weak_ptr = weak_from_this()](const UdpSocket::Error& error) {
        auto ref = weak_ptr.lock();
        if (!ref)
          return;
        auto net_error = net::MapSystemError(error.value());
        connected_ = false;
        CloseAllAcceptedTransports(net_error);
        delegate_->OnTransportClosed(net_error);
      },
  };
}

// AsioUdpTransport

AsioUdpTransport::AsioUdpTransport(UdpSocketFactory udp_socket_factory)
    : udp_socket_factory_{std::move(udp_socket_factory)} {}

Error AsioUdpTransport::Open(Transport::Delegate& delegate) {
  if (core_)
    core_->Close();

  if (active) {
    core_ = std::make_shared<UdpActiveCore>(udp_socket_factory_, host, service);
  } else {
    core_ =
        std::make_shared<UdpPassiveCore>(udp_socket_factory_, host, service);
  }

  core_->Open(delegate);
  return net::OK;
}

std::string AsioUdpTransport::GetName() const {
  return "UDP";
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

  delegate_ = &delegate;
  connected_ = true;

  return OK;
}

void AsioUdpTransport::AcceptedTransport::Close() {
  assert(connected_);
  assert(core_);

  core_->RemoveAcceptedTransport(endpoint_);
  core_ = nullptr;
  connected_ = false;
}

int AsioUdpTransport::AcceptedTransport::Read(void* data, size_t len) {
  assert(false);

  return ERR_FAILED;
}

int AsioUdpTransport::AcceptedTransport::Write(const void* data, size_t len) {
  assert(connected_);
  assert(core_);

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
  if (delegate_)
    delegate_->OnTransportMessageReceived(datagram.data(), datagram.size());
}

void AsioUdpTransport::AcceptedTransport::ProcessError(Error error) {
  core_->RemoveAcceptedTransport(endpoint_);
  core_ = nullptr;
  connected_ = false;

  if (delegate_)
    delegate_->OnTransportClosed(error);
}

}  // namespace net
