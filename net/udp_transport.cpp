#include "udp_transport.h"

#include <queue>

namespace net {

// AsioUdpTransport::UdpCore

class AsioUdpTransport::UdpCore : public Core,
                                  public std::enable_shared_from_this<UdpCore> {
 public:
  UdpCore(boost::asio::io_context& io_context,
          const std::string& host,
          const std::string& service,
          bool active);

  // Core
  virtual bool IsConnected() const override { return connected_; }
  virtual void Open(Delegate& delegate) override;
  virtual void Close() override;
  virtual int Read(void* data, size_t len) override;
  virtual int Write(const void* data, size_t len) override;

 private:
  using Socket = boost::asio::ip::udp::socket;
  using Resolver = boost::asio::ip::udp::resolver;
  using Endpoint = Socket::endpoint_type;
  using Datagram = std::vector<char>;

  void StartReading();
  void StartWriting();

  void ProcessError(const boost::system::error_code& ec);

  boost::asio::io_context& io_context_;

  const std::string host_;
  const std::string service_;
  const bool active_;
  Delegate* delegate_ = nullptr;

  Resolver resolver_{io_context_};
  Endpoint peer_endpoint_;
  Socket socket_{io_context_};

  bool connected_ = false;
  bool closed_ = false;

  Datagram read_buffer_;
  bool reading_ = false;

  std::queue<Datagram> write_queue_;
  Datagram write_buffer_;
  bool writing_ = false;
};

AsioUdpTransport::UdpCore::UdpCore(boost::asio::io_context& io_context,
                                   const std::string& host,
                                   const std::string& service,
                                   bool active)
    : io_context_{io_context},
      host_{host},
      service_{service},
      active_{active},
      read_buffer_(1024 * 1024) {}

void AsioUdpTransport::UdpCore::Open(Delegate& delegate) {
  delegate_ = &delegate;

  Resolver::query query{host_, service_};
  resolver_.async_resolve(query, [this, ref = shared_from_this()](
                                     const boost::system::error_code& error,
                                     Resolver::iterator iterator) {
    if (closed_)
      return;

    if (error) {
      if (error != boost::asio::error::operation_aborted)
        ProcessError(error);
      return;
    }

    boost::system::error_code ec = boost::asio::error::fault;
    for (Resolver::iterator end; iterator != end; ++iterator) {
      socket_.open(iterator->endpoint().protocol(), ec);
      if (ec)
        continue;

      if (active_) {
        peer_endpoint_ = iterator->endpoint();
        break;
      }

      socket_.set_option(Socket::reuse_address{true}, ec);
      socket_.bind(iterator->endpoint(), ec);
      if (!ec)
        break;

      socket_.close();
    }

    if (ec) {
      ProcessError(ec);
      return;
    }

    connected_ = true;
    delegate_->OnTransportOpened();
  });
}

void AsioUdpTransport::UdpCore::Close() {
  if (closed_)
    return;

  closed_ = true;
  connected_ = false;
  writing_ = false;
  write_queue_ = {};
  socket_.close();
}

int AsioUdpTransport::UdpCore::Read(void* data, size_t len) {
  assert(false);
  return net::ERR_FAILED;
}

int AsioUdpTransport::UdpCore::Write(const void* data, size_t len) {
  std::vector<char> datagram(len);
  memcpy(datagram.data(), data, len);
  write_queue_.emplace(std::move(datagram));

  StartWriting();

  return static_cast<int>(len);
}

void AsioUdpTransport::UdpCore::StartReading() {
  if (closed_)
    return;

  if (reading_)
    return;

  reading_ = true;

  socket_.async_receive(
      boost::asio::buffer(read_buffer_),
      [this, ref = shared_from_this()](const boost::system::error_code& error,
                                       std::size_t bytes_transferred) {
        reading_ = false;

        if (closed_)
          return;

        if (error) {
          ProcessError(error);
          return;
        }

        std::vector<char> datagram(read_buffer_.begin(),
                                   read_buffer_.begin() + bytes_transferred);
        delegate_->OnTransportMessageReceived(datagram.data(), datagram.size());

        StartReading();
      });
}

void AsioUdpTransport::UdpCore::StartWriting() {
  if (closed_)
    return;

  if (write_queue_.empty())
    return;

  if (writing_)
    return;

  writing_ = true;

  write_buffer_ = std::move(write_queue_.front());
  write_queue_.pop();

  auto callback = [this, ref = shared_from_this()](
                      const boost::system::error_code& error,
                      std::size_t bytes_transferred) {
    if (closed_)
      return;

    writing_ = false;
    write_buffer_.clear();
    write_buffer_.shrink_to_fit();

    if (error) {
      ProcessError(error);
      return;
    }

    StartReading();
    StartWriting();
  };

  if (active_) {
    socket_.async_send_to(boost::asio::buffer(write_buffer_), peer_endpoint_,
                          std::move(callback));
  } else {
    socket_.async_send(boost::asio::buffer(write_buffer_), std::move(callback));
  }
}

void AsioUdpTransport::UdpCore::ProcessError(
    const boost::system::error_code& ec) {
  connected_ = false;
  closed_ = true;
  writing_ = false;
  write_queue_ = {};
  delegate_->OnTransportClosed(net::MapSystemError(ec.value()));
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
  UdpPassiveCore(boost::asio::io_context& io_context,
                 const std::string& host,
                 const std::string& service);
  ~UdpPassiveCore();

  // Core
  virtual bool IsConnected() const override { return connected_; }
  virtual void Open(Delegate& delegate) override;
  virtual void Close() override;
  virtual int Read(void* data, size_t len) override;
  virtual int Write(const void* data, size_t len) override;

 private:
  using Socket = boost::asio::ip::udp::socket;
  using Resolver = boost::asio::ip::udp::resolver;
  using Endpoint = Socket::endpoint_type;
  using Datagram = std::vector<char>;

  void InternalWrite(const Endpoint& endpoint, Datagram&& datagram);
  void RemoveAcceptedTransport(const Endpoint& endpoint);
  void CloseAllAcceptedTransports(Error error);

  void StartReading();
  void StartWriting();

  void ProcessDatagram(const Endpoint& endpoint, Datagram&& datagram);
  void ProcessError(const boost::system::error_code& ec);

  boost::asio::io_context& io_context_;

  const std::string host_;
  const std::string service_;
  Delegate* delegate_ = nullptr;

  Resolver resolver_{io_context_};
  Socket socket_{io_context_};

  bool connected_ = false;
  bool closed_ = false;
  bool writing_ = false;

  Endpoint read_endpoint_;
  std::vector<char> read_buffer_;

  std::queue<std::pair<Endpoint, std::vector<char>>> write_queue_;
  Datagram write_buffer_;

  std::map<Endpoint, AcceptedTransport*> accepted_transports_;

  friend class AcceptedTransport;
};

AsioUdpTransport::UdpPassiveCore::UdpPassiveCore(
    boost::asio::io_context& io_context,
    const std::string& host,
    const std::string& service)
    : io_context_{io_context},
      host_{host},
      service_{service},
      read_buffer_(1024 * 1024) {}

AsioUdpTransport::UdpPassiveCore::~UdpPassiveCore() {
  assert(accepted_transports_.empty());
}

void AsioUdpTransport::UdpPassiveCore::Open(Delegate& delegate) {
  delegate_ = &delegate;

  Resolver::query query{host_, service_};
  resolver_.async_resolve(query, [this, ref = shared_from_this()](
                                     const boost::system::error_code& error,
                                     Resolver::iterator iterator) {
    if (closed_)
      return;

    if (error) {
      if (error != boost::asio::error::operation_aborted)
        ProcessError(error);
      return;
    }

    boost::system::error_code ec = boost::asio::error::fault;
    for (Resolver::iterator end; iterator != end; ++iterator) {
      socket_.open(iterator->endpoint().protocol(), ec);
      if (ec)
        continue;

      socket_.set_option(Socket::reuse_address{true}, ec);
      socket_.bind(iterator->endpoint(), ec);
      if (!ec)
        break;

      socket_.close();
    }

    if (ec) {
      ProcessError(ec);
      return;
    }

    connected_ = true;
    delegate_->OnTransportOpened();

    StartReading();
  });
}

void AsioUdpTransport::UdpPassiveCore::Close() {
  if (closed_)
    return;

  closed_ = true;
  connected_ = false;
  writing_ = false;
  write_queue_ = {};
  socket_.close();

  CloseAllAcceptedTransports(OK);
}

int AsioUdpTransport::UdpPassiveCore::Read(void* data, size_t len) {
  return ERR_FAILED;
}

int AsioUdpTransport::UdpPassiveCore::Write(const void* data, size_t len) {
  return ERR_FAILED;
}

void AsioUdpTransport::UdpPassiveCore::InternalWrite(const Endpoint& endpoint,
                                                     Datagram&& datagram) {
  write_queue_.emplace(endpoint, std::move(datagram));

  StartWriting();
}

void AsioUdpTransport::UdpPassiveCore::RemoveAcceptedTransport(
    const Endpoint& endpoint) {
  assert(accepted_transports_.find(endpoint) != accepted_transports_.end());

  accepted_transports_.erase(endpoint);
}

void AsioUdpTransport::UdpPassiveCore::StartReading() {
  if (closed_)
    return;

  socket_.async_receive_from(
      boost::asio::buffer(read_buffer_), read_endpoint_,
      [this, ref = shared_from_this()](const boost::system::error_code& error,
                                       std::size_t bytes_transferred) {
        if (closed_)
          return;

        if (error) {
          ProcessError(error);
          return;
        }

        std::vector<char> datagram(read_buffer_.begin(),
                                   read_buffer_.begin() + bytes_transferred);
        ProcessDatagram(read_endpoint_, std::move(datagram));

        StartReading();
      });
}

void AsioUdpTransport::UdpPassiveCore::StartWriting() {
  if (closed_)
    return;

  if (write_queue_.empty())
    return;

  if (writing_)
    return;

  writing_ = true;

  auto [endpoint, datagram] = std::move(write_queue_.front());
  write_queue_.pop();

  write_buffer_ = std::move(datagram);

  socket_.async_send_to(
      boost::asio::buffer(write_buffer_), endpoint,
      [this, ref = shared_from_this(), datagram = std::move(datagram)](
          const boost::system::error_code& error,
          std::size_t bytes_transferred) {
        if (closed_)
          return;

        writing_ = false;
        write_buffer_.clear();
        write_buffer_.shrink_to_fit();

        if (error) {
          ProcessError(error);
          return;
        }

        StartWriting();
      });
}

void AsioUdpTransport::UdpPassiveCore::ProcessDatagram(const Endpoint& endpoint,
                                                       Datagram&& datagram) {
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

void AsioUdpTransport::UdpPassiveCore::ProcessError(
    const boost::system::error_code& ec) {
  connected_ = false;
  closed_ = true;
  writing_ = false;
  write_queue_ = {};

  auto error = net::MapSystemError(ec.value());

  CloseAllAcceptedTransports(error);

  delegate_->OnTransportClosed(error);
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

// AsioUdpTransport

AsioUdpTransport::AsioUdpTransport(boost::asio::io_context& io_context)
    : AsioTransport{io_context} {}

AsioUdpTransport::~AsioUdpTransport() {
  if (core_)
    core_->Close();
}

Error AsioUdpTransport::Open(Transport::Delegate& delegate) {
  if (!core_) {
    if (active)
      core_ = std::make_shared<UdpCore>(io_context_, host, service, active);
    else
      core_ = std::make_shared<UdpPassiveCore>(io_context_, host, service);
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
  /*if (read_queue_.empty())
    return 0;

  auto& datagram = read_queue_.front();
  if (datagram.size() > len)
    return net::ERR_SOCKET_RECEIVE_BUFFER_SIZE_UNCHANGEABLE;

  memcpy(data, datagram.data(), len);
  read_queue_.pop();

  return static_cast<int>(len);*/

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
