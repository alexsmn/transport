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

// AcceptedUdpTransport

class AcceptedUdpTransport final : public Transport {
 public:
  class UdpAcceptedCore;

  explicit AcceptedUdpTransport(std::shared_ptr<UdpAcceptedCore> core);

  ~AcceptedUdpTransport();

  [[nodiscard]] virtual std::string name() const override;
  [[nodiscard]] virtual bool active() const override { return false; }
  [[nodiscard]] virtual bool connected() const override;
  [[nodiscard]] virtual bool message_oriented() const override { return true; }
  [[nodiscard]] virtual executor get_executor() override;

  [[nodiscard]] virtual awaitable<error_code> open() override;
  [[nodiscard]] virtual awaitable<error_code> close() override;
  [[nodiscard]] virtual awaitable<expected<any_transport>> accept() override;

  [[nodiscard]] virtual awaitable<expected<size_t>> read(
      std::span<char> data) override;

  [[nodiscard]] virtual awaitable<expected<size_t>> write(
      std::span<const char> data) override;

 private:
  std::shared_ptr<UdpAcceptedCore> core_;
};

// UdpTransportCore

class UdpTransportCore {
 public:
  virtual ~UdpTransportCore() = default;

  [[nodiscard]] virtual executor get_executor() = 0;
  [[nodiscard]] virtual bool connected() const = 0;

  [[nodiscard]] virtual awaitable<error_code> open() = 0;
  [[nodiscard]] virtual awaitable<error_code> close() = 0;
  [[nodiscard]] virtual awaitable<expected<any_transport>> accept() = 0;

  [[nodiscard]] virtual awaitable<expected<size_t>> read(
      std::span<char> data) = 0;

  [[nodiscard]] virtual awaitable<expected<size_t>> write(
      std::span<const char> data) = 0;

  virtual void shutdown() = 0;
};

// ActiveUdpTransport::UdpActiveCore

class ActiveUdpTransport::UdpActiveCore final
    : public UdpTransportCore,
      public std::enable_shared_from_this<UdpActiveCore> {
 public:
  UdpActiveCore(const executor& executor,
                UdpSocketFactory udp_socket_factory,
                std::string host,
                std::string service);

  // UdpTransportCore
  virtual executor get_executor() override { return executor_; }
  virtual bool connected() const override { return connected_; }

  [[nodiscard]] virtual awaitable<error_code> open() override;
  [[nodiscard]] virtual awaitable<error_code> close() override;
  [[nodiscard]] virtual awaitable<expected<any_transport>> accept() override;
  [[nodiscard]] virtual awaitable<expected<size_t>> read(
      std::span<char> data) override;
  [[nodiscard]] virtual awaitable<expected<size_t>> write(
      std::span<const char> data) override;
  virtual void shutdown() override;

 private:
  UdpSocketContext MakeUdpSocketImplContext();

  void OnSocketOpened(const UdpSocket::Endpoint& endpoint);
  void OnSocketMessage(const UdpSocket::Endpoint& endpoint,
                       UdpSocket::Datagram&& datagram);
  void OnSocketClosed(const UdpSocket::error_code& error);

  const executor executor_;
  const UdpSocketFactory udp_socket_factory_;
  const std::string host_;
  const std::string service_;

  std::shared_ptr<UdpSocket> socket_;

  bool connected_ = false;
  UdpSocket::Endpoint peer_endpoint_;

  boost::asio::experimental::channel<void(boost::system::error_code,
                                          UdpSocket::Datagram datagram)>
      read_channel_{executor_,
                    /*max_buffer_size=*/std::numeric_limits<size_t>::max()};
};

ActiveUdpTransport::UdpActiveCore::UdpActiveCore(
    const executor& executor,
    UdpSocketFactory udp_socket_factory,
    std::string host,
    std::string service)
    : executor_{executor},
      udp_socket_factory_{std::move(udp_socket_factory)},
      host_{std::move(host)},
      service_{std::move(service)} {}

void ActiveUdpTransport::UdpActiveCore::shutdown() {
  socket_->Shutdown();
}

awaitable<error_code> ActiveUdpTransport::UdpActiveCore::open() {
  socket_ = udp_socket_factory_(MakeUdpSocketImplContext());

  return socket_->Open();
}

awaitable<error_code> ActiveUdpTransport::UdpActiveCore::close() {
  if (!socket_) {
    co_return ERR_INVALID_HANDLE;
  }

  auto ref = shared_from_this();

  co_await boost::asio::dispatch(executor_, boost::asio::use_awaitable);

  connected_ = false;

  co_await socket_->Close();

  co_return OK;
}

awaitable<expected<any_transport>> ActiveUdpTransport::UdpActiveCore::accept() {
  co_return ERR_INVALID_ARGUMENT;
}

awaitable<expected<size_t>> ActiveUdpTransport::UdpActiveCore::read(
    std::span<char> data) {
  auto ref = shared_from_this();

  auto [ec, datagram] = co_await read_channel_.async_receive(
      boost::asio::as_tuple(boost::asio::use_awaitable));

  if (ec) {
    co_return ec;
  }

  if (datagram.size() > data.size()) {
    co_return ERR_INVALID_ARGUMENT;
  }

  std::ranges::copy(datagram, data.begin());
  co_return datagram.size();
}

awaitable<expected<size_t>> ActiveUdpTransport::UdpActiveCore::write(
    std::span<const char> data) {
  return socket_->SendTo(peer_endpoint_, data);
}

void ActiveUdpTransport::UdpActiveCore::OnSocketOpened(
    const UdpSocket::Endpoint& endpoint) {
  peer_endpoint_ = endpoint;
  connected_ = true;
}

void ActiveUdpTransport::UdpActiveCore::OnSocketMessage(
    const UdpSocket::Endpoint& endpoint,
    UdpSocket::Datagram&& datagram) {
  // TODO: Handle fail.
  read_channel_.try_send(boost::system::error_code{}, std::move(datagram));
}

void ActiveUdpTransport::UdpActiveCore::OnSocketClosed(
    const UdpSocket::error_code& error) {
  connected_ = false;
}

UdpSocketContext ActiveUdpTransport::UdpActiveCore::MakeUdpSocketImplContext() {
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
      [weak_ptr = weak_from_this()](const UdpSocket::error_code& error) {
        if (auto ref = weak_ptr.lock())
          ref->OnSocketClosed(error);
      },
  };
}

// AcceptedUdpTransport::UdpAcceptedCore

class AcceptedUdpTransport::UdpAcceptedCore final
    : public UdpTransportCore,
      public std::enable_shared_from_this<UdpAcceptedCore> {
 public:
  UdpAcceptedCore(
      const executor& executor,
      const log_source& log,
      std::shared_ptr<PassiveUdpTransport::UdpPassiveCore> passive_core,
      UdpSocket::Endpoint endpoint);
  ~UdpAcceptedCore();

  // UdpTransportCore
  virtual executor get_executor() override { return executor_; }
  virtual bool connected() const override { return connected_; }
  virtual awaitable<error_code> open() override;
  virtual awaitable<error_code> close() override;
  virtual awaitable<expected<any_transport>> accept() override;
  virtual awaitable<expected<size_t>> read(std::span<char> data) override;
  virtual awaitable<expected<size_t>> write(
      std::span<const char> data) override;
  virtual void shutdown() override;

 private:
  void OnSocketMessage(const UdpSocket::Endpoint& endpoint,
                       UdpSocket::Datagram&& datagram);
  void OnSocketClosed(const UdpSocket::error_code& error);

  executor executor_;
  log_source log_;
  std::shared_ptr<PassiveUdpTransport::UdpPassiveCore> passive_core_;
  UdpSocket::Endpoint endpoint_;

  bool connected_ = true;

  boost::asio::experimental::channel<void(boost::system::error_code,
                                          std::vector<char> data)>
      received_message_channel_{
          executor_,
          /*max_buffer_size=*/std::numeric_limits<size_t>::max()};

  friend class PassiveUdpTransport::UdpPassiveCore;
};

// PassiveUdpTransport::UdpPassiveCore

class PassiveUdpTransport::UdpPassiveCore final
    : public UdpTransportCore,
      public std::enable_shared_from_this<UdpPassiveCore> {
 public:
  UdpPassiveCore(const executor& executor,
                 const log_source& log,
                 UdpSocketFactory udp_socket_factory,
                 std::string host,
                 std::string service);
  ~UdpPassiveCore();

  // UdpTransportCore
  virtual executor get_executor() override { return executor_; }
  virtual bool connected() const override { return connected_; }
  virtual awaitable<error_code> open() override;
  virtual awaitable<error_code> close() override;
  virtual awaitable<expected<any_transport>> accept() override;
  virtual awaitable<expected<size_t>> read(std::span<char> data) override;
  virtual awaitable<expected<size_t>> write(
      std::span<const char> data) override;
  virtual void shutdown() override;

 private:
  UdpSocketContext MakeUdpSocketImplContext();

  void OnSocketOpened(const UdpSocket::Endpoint& endpoint);
  void OnSocketMessage(const UdpSocket::Endpoint& endpoint,
                       UdpSocket::Datagram&& datagram);
  void OnSocketClosed(const UdpSocket::error_code& error);

  [[nodiscard]] awaitable<expected<size_t>> InternalWrite(
      UdpSocket::Endpoint endpoint,
      std::span<const char> datagram);

  void RemoveAcceptedTransport(const UdpSocket::Endpoint& endpoint);
  void CloseAllAcceptedTransports(error_code error);

  const executor executor_;
  const log_source log_;
  const UdpSocketFactory udp_socket_factory_;
  const std::string host_;
  const std::string service_;

  std::shared_ptr<UdpSocket> socket_;

  bool connected_ = false;

  std::map<UdpSocket::Endpoint,
           std::shared_ptr<AcceptedUdpTransport::UdpAcceptedCore>>
      accepted_transports_;

  boost::asio::experimental::channel<void(
      boost::system::error_code,
      std::shared_ptr<AcceptedUdpTransport::UdpAcceptedCore>)>
      accept_channel_{executor_,
                      /*max_buffer_size=*/std::numeric_limits<size_t>::max()};

  friend class AcceptedUdpTransport::UdpAcceptedCore;
};

// PassiveUdpTransport::UdpPassiveCore

PassiveUdpTransport::UdpPassiveCore::UdpPassiveCore(
    const executor& executor,
    const log_source& log,
    UdpSocketFactory udp_socket_factory,
    std::string host,
    std::string service)
    : executor_{executor},
      log_{log},
      udp_socket_factory_{std::move(udp_socket_factory)},
      host_{std::move(host)},
      service_{std::move(service)} {}

PassiveUdpTransport::UdpPassiveCore::~UdpPassiveCore() {
  assert(accepted_transports_.empty());
}

void PassiveUdpTransport::UdpPassiveCore::shutdown() {
  socket_->Shutdown();
  CloseAllAcceptedTransports(ERR_ABORTED);
}

awaitable<error_code> PassiveUdpTransport::UdpPassiveCore::open() {
  log_.writef(LogSeverity::Normal, "Open");

  socket_ = udp_socket_factory_(MakeUdpSocketImplContext());

  return socket_->Open();
}

awaitable<error_code> PassiveUdpTransport::UdpPassiveCore::close() {
  auto ref = shared_from_this();

  co_await boost::asio::dispatch(executor_, boost::asio::use_awaitable);

  log_.write(LogSeverity::Normal, "Close");

  connected_ = false;

  co_await socket_->Close();

  CloseAllAcceptedTransports(OK);

  co_return OK;
}

awaitable<expected<any_transport>>
PassiveUdpTransport::UdpPassiveCore::accept() {
  auto ref = shared_from_this();

  auto [ec, accepted_core] = co_await accept_channel_.async_receive(
      boost::asio::experimental::as_tuple(boost::asio::use_awaitable));

  if (ec) {
    co_return ec;
  }

  co_return any_transport{std::unique_ptr<AcceptedUdpTransport>(
      new AcceptedUdpTransport{std::move(accepted_core)})};
}

awaitable<expected<size_t>> PassiveUdpTransport::UdpPassiveCore::read(
    std::span<char> data) {
  co_return ERR_FAILED;
}

awaitable<expected<size_t>> PassiveUdpTransport::UdpPassiveCore::write(
    std::span<const char> data) {
  assert(false);
  co_return ERR_FAILED;
}

awaitable<expected<size_t>> PassiveUdpTransport::UdpPassiveCore::InternalWrite(
    UdpSocket::Endpoint endpoint,
    std::span<const char> datagram) {
  return socket_->SendTo(std::move(endpoint), datagram);
}

void PassiveUdpTransport::UdpPassiveCore::RemoveAcceptedTransport(
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

void PassiveUdpTransport::UdpPassiveCore::OnSocketMessage(
    const UdpSocket::Endpoint& endpoint,
    UdpSocket::Datagram&& datagram) {
  std::shared_ptr<AcceptedUdpTransport::UdpAcceptedCore> accepted_core;

  if (auto i = accepted_transports_.find(endpoint);
      i != accepted_transports_.end()) {
    accepted_core = i->second;
  } else {
    log_.writef(LogSeverity::Normal,
                "Accept new transport from endpoint %s. There are %d accepted "
                "transports",
                ToString(endpoint).c_str(),
                static_cast<int>(accepted_transports_.size()));

    accepted_core = std::make_shared<AcceptedUdpTransport::UdpAcceptedCore>(
        executor_, log_, shared_from_this(), endpoint);

    accepted_transports_.insert_or_assign(endpoint, accepted_core);

    bool posted =
        accept_channel_.try_send(boost::system::error_code{}, accepted_core);

    if (!posted) {
      log_.write(LogSeverity::Error, "Accept queue is full");
      return;
    }
  }

  boost::asio::dispatch(
      accepted_core->get_executor(),
      [accepted_core, endpoint, datagram = std::move(datagram)]() mutable {
        accepted_core->OnSocketMessage(endpoint, std::move(datagram));
      });
}

void PassiveUdpTransport::UdpPassiveCore::CloseAllAcceptedTransports(
    error_code error) {
  log_.writef(LogSeverity::Normal, "Close %d accepted transports - %s",
              static_cast<int>(accepted_transports_.size()),
              ErrorToString(error).c_str());

  std::vector<std::shared_ptr<AcceptedUdpTransport::UdpAcceptedCore>>
      accepted_cores;
  for (const auto& [endpoint, accepted_core] : accepted_transports_) {
    accepted_cores.push_back(accepted_core);
  }

  for (const auto& accepted_core : accepted_cores) {
    boost::asio::dispatch(accepted_core->get_executor(), [accepted_core] {
      accepted_core->OnSocketClosed(UdpSocket::error_code{});
    });
  }
}

void PassiveUdpTransport::UdpPassiveCore::OnSocketOpened(
    const UdpSocket::Endpoint& endpoint) {
  log_.writef(LogSeverity::Normal, "Opened with endpoint %s",
              ToString(endpoint).c_str());

  connected_ = true;
}

void PassiveUdpTransport::UdpPassiveCore::OnSocketClosed(
    const UdpSocket::error_code& error) {
  log_.writef(LogSeverity::Normal, "Closed - %s", error.message().c_str());

  connected_ = false;
  CloseAllAcceptedTransports(error);
}

UdpSocketContext
PassiveUdpTransport::UdpPassiveCore::MakeUdpSocketImplContext() {
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
      [weak_ptr = weak_from_this()](const UdpSocket::error_code& error) {
        if (auto ref = weak_ptr.lock())
          ref->OnSocketClosed(error);
      },
  };
}

// AcceptedUdpTransport::UdpAcceptedCore

AcceptedUdpTransport::UdpAcceptedCore::UdpAcceptedCore(
    const executor& executor,
    const log_source& log,
    std::shared_ptr<PassiveUdpTransport::UdpPassiveCore> passive_core,
    UdpSocket::Endpoint endpoint)
    : executor_{executor},
      log_{log},
      passive_core_{std::move(passive_core)},
      endpoint_{std::move(endpoint)} {
  assert(passive_core_);
}

AcceptedUdpTransport::UdpAcceptedCore::~UdpAcceptedCore() {
  if (passive_core_) {
    passive_core_->RemoveAcceptedTransport(endpoint_);
  }
}

void AcceptedUdpTransport::UdpAcceptedCore::shutdown() {}

awaitable<error_code> AcceptedUdpTransport::UdpAcceptedCore::open() {
  co_return ERR_ADDRESS_IN_USE;
}

awaitable<error_code> AcceptedUdpTransport::UdpAcceptedCore::close() {
  if (passive_core_) {
    passive_core_->RemoveAcceptedTransport(endpoint_);
    passive_core_ = nullptr;
  }

  connected_ = false;

  co_return OK;
}

awaitable<expected<any_transport>>
AcceptedUdpTransport::UdpAcceptedCore::accept() {
  co_return ERR_FAILED;
}

awaitable<expected<size_t>> AcceptedUdpTransport::UdpAcceptedCore::read(
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

awaitable<expected<size_t>> AcceptedUdpTransport::UdpAcceptedCore::write(
    std::span<const char> data) {
  if (!passive_core_ || !connected_) {
    co_return ERR_CONNECTION_CLOSED;
  }

  co_return co_await passive_core_->InternalWrite(endpoint_, data);
}

void AcceptedUdpTransport::UdpAcceptedCore::OnSocketMessage(
    const UdpSocket::Endpoint& endpoint,
    UdpSocket::Datagram&& datagram) {
  bool posted = received_message_channel_.try_send(boost::system::error_code{},
                                                   std::move(datagram));

  if (!posted) {
    log_.write(LogSeverity::Error, "Received message queue is full");
    return;
  }
}

void AcceptedUdpTransport::UdpAcceptedCore::OnSocketClosed(
    const UdpSocket::error_code& error) {
  assert(passive_core_);

  if (passive_core_) {
    passive_core_->RemoveAcceptedTransport(endpoint_);
    passive_core_ = nullptr;
  }

  connected_ = false;
}

// ActiveUdpTransport

ActiveUdpTransport::ActiveUdpTransport(const executor& executor,
                                       const log_source& log,
                                       UdpSocketFactory udp_socket_factory,
                                       std::string host,
                                       std::string service) {
  core_ =
      std::make_shared<UdpActiveCore>(executor, std::move(udp_socket_factory),
                                      std::move(host), std::move(service));
}

ActiveUdpTransport::~ActiveUdpTransport() {
  boost::asio::dispatch(core_->get_executor(),
                        [core = core_] { core->shutdown(); });
}

executor ActiveUdpTransport::get_executor() {
  return core_->get_executor();
}

bool ActiveUdpTransport::connected() const {
  return core_->connected();
}

awaitable<error_code> ActiveUdpTransport::open() {
  return core_->open();
}

awaitable<error_code> ActiveUdpTransport::close() {
  return core_->close();
}

std::string ActiveUdpTransport::name() const {
  return "UDP";
}

awaitable<expected<any_transport>> ActiveUdpTransport::accept() {
  return core_->accept();
}

awaitable<expected<size_t>> ActiveUdpTransport::read(std::span<char> data) {
  return core_->read(data);
}

awaitable<expected<size_t>> ActiveUdpTransport::write(
    std::span<const char> data) {
  return core_->write(data);
}

// PassiveUdpTransport

PassiveUdpTransport::PassiveUdpTransport(const executor& executor,
                                         const log_source& log,
                                         UdpSocketFactory udp_socket_factory,
                                         std::string host,
                                         std::string service) {
  core_ = std::make_shared<UdpPassiveCore>(executor, log,
                                           std::move(udp_socket_factory),
                                           std::move(host), std::move(service));
}

PassiveUdpTransport::~PassiveUdpTransport() {
  boost::asio::co_spawn(
      core_->get_executor(), [core = core_] { return core->close(); },
      boost::asio::detached);
}

executor PassiveUdpTransport::get_executor() {
  return core_->get_executor();
}

bool PassiveUdpTransport::connected() const {
  return core_->connected();
}

awaitable<error_code> PassiveUdpTransport::open() {
  return core_->open();
}

awaitable<error_code> PassiveUdpTransport::close() {
  return core_->close();
}

std::string PassiveUdpTransport::name() const {
  return "UDP";
}

awaitable<expected<any_transport>> PassiveUdpTransport::accept() {
  return core_->accept();
}

awaitable<expected<size_t>> PassiveUdpTransport::read(std::span<char> data) {
  return core_->read(data);
}

awaitable<expected<size_t>> PassiveUdpTransport::write(
    std::span<const char> data) {
  return core_->write(data);
}

// AcceptedUdpTransport

AcceptedUdpTransport::AcceptedUdpTransport(
    std::shared_ptr<UdpAcceptedCore> core)
    : core_{std::move(core)} {}

AcceptedUdpTransport::~AcceptedUdpTransport() {
  boost::asio::dispatch(core_->get_executor(),
                        [core = core_] { core->shutdown(); });
}

executor AcceptedUdpTransport::get_executor() {
  return core_->get_executor();
}

bool AcceptedUdpTransport::connected() const {
  return core_->connected();
}

awaitable<error_code> AcceptedUdpTransport::open() {
  return core_->open();
}

awaitable<error_code> AcceptedUdpTransport::close() {
  return core_->close();
}

std::string AcceptedUdpTransport::name() const {
  return "UDP";
}

awaitable<expected<any_transport>> AcceptedUdpTransport::accept() {
  return core_->accept();
}

awaitable<expected<size_t>> AcceptedUdpTransport::read(std::span<char> data) {
  return core_->read(data);
}

awaitable<expected<size_t>> AcceptedUdpTransport::write(
    std::span<const char> data) {
  return core_->write(data);
}

}  // namespace transport
