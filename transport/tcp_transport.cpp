#include "transport/tcp_transport.h"

#include "transport/log.h"

#include <boost/asio/connect.hpp>

namespace transport {

// TcpTransport::ActiveCore

class TcpTransport::ActiveCore final
    : public IoCore<boost::asio::ip::tcp::socket> {
 public:
  using Socket = boost::asio::ip::tcp::socket;

  ActiveCore(const Executor& executor,
             const log_source& log,
             const std::string& host,
             const std::string& service);

  // A constructor for a socket accepted by a passive TCP transport.
  // Uses the executor of the socket.
  ActiveCore(Socket socket, const log_source& log);

  // Core
  virtual awaitable<Error> open() override;

 private:
  using Resolver = boost::asio::ip::tcp::resolver;

  [[nodiscard]] awaitable<Error> ResolveAndConnect();
  [[nodiscard]] awaitable<Error> Connect(Resolver::iterator iterator);

  // ActiveCore
  virtual void Cleanup() override;

  std::string host_;
  std::string service_;

  Resolver resolver_;
};

TcpTransport::ActiveCore::ActiveCore(const Executor& executor,
                                     const log_source& log,
                                     const std::string& host,
                                     const std::string& service)
    : IoCore{executor, log},
      host_{host},
      service_{service},
      resolver_{executor} {}

TcpTransport::ActiveCore::ActiveCore(Socket socket, const log_source& log)
    : IoCore{socket.get_executor(), log}, resolver_{socket.get_executor()} {
  io_object_ = std::move(socket);
  connected_ = true;
}

awaitable<Error> TcpTransport::ActiveCore::open() {
  auto ref = std::static_pointer_cast<ActiveCore>(shared_from_this());

  if (connected_) {
    co_return OK;
  }

  log_.writef(LogSeverity::Normal, "Open");

  co_return co_await ResolveAndConnect();
}

awaitable<Error> TcpTransport::ActiveCore::ResolveAndConnect() {
  auto ref = shared_from_this();

  log_.writef(LogSeverity::Normal, "Start DNS resolution to %s:%s",
              host_.c_str(), service_.c_str());

  auto [error, iterator] = co_await resolver_.async_resolve(
      host_, service_, boost::asio::as_tuple(boost::asio::use_awaitable));

  if (closed_) {
    co_return ERR_ABORTED;
  }

  if (error) {
    if (error != boost::asio::error::operation_aborted) {
      log_.write(LogSeverity::Warning, "DNS resolution error");
      ProcessError(error);
    }
    co_return error;
  }

  log_.write(LogSeverity::Normal, "DNS resolution completed");

  co_return co_await Connect(std::move(iterator));
}

awaitable<Error> TcpTransport::ActiveCore::Connect(
    Resolver::iterator iterator) {
  auto ref = std::static_pointer_cast<ActiveCore>(shared_from_this());

  auto [error, connected_iterator] = co_await boost::asio::async_connect(
      io_object_, iterator, boost::asio::as_tuple(boost::asio::use_awaitable));

  if (closed_) {
    co_return ERR_ABORTED;
  }

  if (error) {
    if (error != boost::asio::error::operation_aborted) {
      log_.write(LogSeverity::Warning, "Connect error");
      ProcessError(error);
    }
    co_return error;
  }

  log_.writef(LogSeverity::Normal, "Connected to %s",
              connected_iterator->host_name().c_str());

  connected_ = true;

  co_return OK;
}

void TcpTransport::ActiveCore::Cleanup() {
  assert(closed_);

  log_.write(LogSeverity::Normal, "Cleanup");

  connected_ = false;

  resolver_.cancel();

  boost::system::error_code ec;
  io_object_.cancel(ec);
  io_object_.close(ec);
}

// TcpTransport::PassiveCore

class TcpTransport::PassiveCore final
    : public Core,
      public std::enable_shared_from_this<PassiveCore> {
 public:
  // The executor must be a sequential one.
  PassiveCore(const Executor& executor,
              const log_source& logger,
              const std::string& host,
              const std::string& service);

  int GetLocalPort() const;

  // Core
  virtual awaitable<Error> open() override;
  virtual awaitable<Error> close() override;
  virtual Executor get_executor() override { return acceptor_.get_executor(); }
  virtual bool connected() const override { return connected_; }

  [[nodiscard]] virtual awaitable<ErrorOr<any_transport>> accept() override;

  [[nodiscard]] virtual awaitable<ErrorOr<size_t>> read(
      std::span<char> data) override;

  [[nodiscard]] virtual awaitable<ErrorOr<size_t>> write(
      std::span<const char> data) override;

 private:
  using Socket = boost::asio::ip::tcp::socket;
  using Resolver = boost::asio::ip::tcp::resolver;

  [[nodiscard]] awaitable<Error> ResolveAndBind();
  [[nodiscard]] boost::system::error_code Bind(Resolver::iterator iterator);

  void ProcessError(const boost::system::error_code& ec);

  log_source log_;
  std::string host_;
  std::string service_;

  Resolver resolver_;
  boost::asio::ip::tcp::acceptor acceptor_;

  bool connected_ = false;
  bool closed_ = false;
};

TcpTransport::PassiveCore::PassiveCore(const Executor& executor,
                                       const log_source& log,
                                       const std::string& host,
                                       const std::string& service)
    : log_{log},
      host_{host},
      service_{service},
      resolver_{executor},
      acceptor_{executor} {}

int TcpTransport::PassiveCore::GetLocalPort() const {
  return acceptor_.local_endpoint().port();
}

awaitable<Error> TcpTransport::PassiveCore::open() {
  auto ref = shared_from_this();

  log_.writef(LogSeverity::Normal, "Open");

  return ResolveAndBind();
}

awaitable<Error> TcpTransport::PassiveCore::ResolveAndBind() {
  log_.writef(LogSeverity::Normal, "Start DNS resolution to %s:%s",
              host_.c_str(), service_.c_str());

  auto ref = shared_from_this();

  auto [error, iterator] = co_await resolver_.async_resolve(
      /*query=*/{host_, service_},
      boost::asio::as_tuple(boost::asio::use_awaitable));

  if (closed_) {
    co_return ERR_ABORTED;
  }

  if (error) {
    if (error != boost::asio::error::operation_aborted) {
      log_.write(LogSeverity::Warning, "DNS resolution error");
      ProcessError(error);
    }
    co_return error;
  }

  log_.write(LogSeverity::Normal, "DNS resolution completed");

  if (auto ec = Bind(std::move(iterator)); ec) {
    log_.write(LogSeverity::Warning, "Bind error");
    ProcessError(ec);
    co_return error;
  }

  log_.write(LogSeverity::Normal, "Bind completed");

  connected_ = true;

  co_return OK;
}

boost::system::error_code TcpTransport::PassiveCore::Bind(
    Resolver::iterator iterator) {
  log_.write(LogSeverity::Normal, "Bind");

  boost::system::error_code ec = boost::asio::error::fault;

  for (Resolver::iterator end; iterator != end; ++iterator) {
    acceptor_.open(iterator->endpoint().protocol(), ec);
    if (ec)
      continue;

    acceptor_.set_option(Socket::reuse_address{true}, ec);
    // TODO: Log endpoint.
    acceptor_.bind(iterator->endpoint(), ec);

    if (!ec)
      acceptor_.listen(Socket::max_listen_connections, ec);

    if (!ec)
      break;

    acceptor_.close();
  }

  return ec;
}

awaitable<Error> TcpTransport::PassiveCore::close() {
  auto ref = shared_from_this();

  co_await boost::asio::dispatch(acceptor_.get_executor(),
                                 boost::asio::use_awaitable);

  if (closed_) {
    co_return ERR_CONNECTION_CLOSED;
  }

  log_.write(LogSeverity::Normal, "Close");

  closed_ = true;
  connected_ = false;
  acceptor_.close();

  co_return OK;
}

awaitable<ErrorOr<any_transport>> TcpTransport::PassiveCore::accept() {
  auto ref = shared_from_this();

  // TODO: Use different executor.
  auto [error, peer] = co_await acceptor_.async_accept(
      boost::asio::as_tuple(boost::asio::use_awaitable));

  assert(peer.get_executor() == acceptor_.get_executor());

  if (closed_) {
    co_return ERR_ABORTED;
  }

  // TODO: Log connection information.
  log_.write(LogSeverity::Normal, "Accept incoming connection");

  if (error) {
    if (error != boost::asio::error::operation_aborted) {
      log_.write(LogSeverity::Warning, "Accept connection error");
      ProcessError(error);
    }
    co_return error;
  }

  log_.write(LogSeverity::Normal, "Connection accepted");

  co_return any_transport{
      std::make_unique<TcpTransport>(std::move(peer), log_)};
}

awaitable<ErrorOr<size_t>> TcpTransport::PassiveCore::read(
    std::span<char> data) {
  co_return ERR_ACCESS_DENIED;
}

awaitable<ErrorOr<size_t>> TcpTransport::PassiveCore::write(
    std::span<const char> data) {
  co_return ERR_ACCESS_DENIED;
}

void TcpTransport::PassiveCore::ProcessError(
    const boost::system::error_code& ec) {
  if (closed_) {
    return;
  }

  if (ec != OK) {
    log_.writef(LogSeverity::Warning, "Error: %s",
                ErrorToShortString(ec).c_str());
  } else {
    log_.writef(LogSeverity::Normal, "Graceful close");
  }

  connected_ = false;
  closed_ = true;
}

// TcpTransport

TcpTransport::TcpTransport(const Executor& executor,
                           const log_source& log,
                           std::string host,
                           std::string service,
                           bool active)
    : type_{active ? Type::ACTIVE : Type::PASSIVE} {
  if (active) {
    core_ = std::make_shared<ActiveCore>(executor, log, std::move(host),
                                         std::move(service));
  } else {
    core_ = std::make_shared<PassiveCore>(executor, log, std::move(host),
                                          std::move(service));
  }
}

TcpTransport::TcpTransport(boost::asio::ip::tcp::socket socket,
                           const log_source& log)
    : type_{Type::ACCEPTED} {
  core_ = std::make_shared<ActiveCore>(std::move(socket), log);
}

TcpTransport::~TcpTransport() {
  // The base class closes the core on destruction.
}

awaitable<Error> TcpTransport::open() {
  co_return co_await core_->open();
}

awaitable<ErrorOr<any_transport>> TcpTransport::accept() {
  co_return co_await core_->accept();
}

int TcpTransport::GetLocalPort() const {
  return core_ && type_ == Type::PASSIVE
             ? std::static_pointer_cast<PassiveCore>(core_)->GetLocalPort()
             : 0;
}

std::string TcpTransport::name() const {
  switch (type_) {
    case Type::ACTIVE:
      return "TCP Active";
    case Type::PASSIVE:
      return "TCP Passive";
    case Type::ACCEPTED:
      return "TCP Accepted";
    default:
      assert(false);
      return "TCP Unknown";
  }
}

}  // namespace transport
