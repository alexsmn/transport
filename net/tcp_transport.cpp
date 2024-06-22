#include "net/tcp_transport.h"

#include "net/logger.h"

namespace net {

// AsioTcpTransport::ActiveCore

class AsioTcpTransport::ActiveCore final
    : public IoCore<boost::asio::ip::tcp::socket> {
 public:
  using Socket = boost::asio::ip::tcp::socket;

  ActiveCore(const Executor& executor,
             std::shared_ptr<const Logger> logger,
             const std::string& host,
             const std::string& service);

  // A constructor for a socket accepted by a passive TCP transport.
  // Uses the executor of the socket.
  ActiveCore(std::shared_ptr<const Logger> logger, Socket socket);

  // Core
  virtual awaitable<Error> Open() override;

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

AsioTcpTransport::ActiveCore::ActiveCore(
    const boost::asio::any_io_executor& executor,
    std::shared_ptr<const Logger> logger,
    const std::string& host,
    const std::string& service)
    : IoCore{executor, std::move(logger)},
      host_{host},
      service_{service},
      resolver_{executor} {}

AsioTcpTransport::ActiveCore::ActiveCore(std::shared_ptr<const Logger> logger,
                                         Socket socket)
    : IoCore{socket.get_executor(), std::move(logger)},
      resolver_{socket.get_executor()} {
  io_object_ = std::move(socket);
  connected_ = true;
}

awaitable<Error> AsioTcpTransport::ActiveCore::Open() {
  auto ref = std::static_pointer_cast<ActiveCore>(shared_from_this());

  if (connected_) {
    co_return OK;
  }

  logger_->WriteF(LogSeverity::Normal, "Open");

  co_return co_await ResolveAndConnect();
}

awaitable<Error> AsioTcpTransport::ActiveCore::ResolveAndConnect() {
  auto ref = shared_from_this();

  logger_->WriteF(LogSeverity::Normal, "Start DNS resolution to %s:%s",
                  host_.c_str(), service_.c_str());

  auto [error, iterator] = co_await resolver_.async_resolve(
      host_, service_, boost::asio::as_tuple(boost::asio::use_awaitable));

  if (closed_) {
    co_return ERR_ABORTED;
  }

  if (error) {
    if (error != boost::asio::error::operation_aborted) {
      logger_->Write(LogSeverity::Warning, "DNS resolution error");
      ProcessError(error);
    }
    co_return error;
  }

  logger_->Write(LogSeverity::Normal, "DNS resolution completed");

  co_return co_await Connect(std::move(iterator));
}

awaitable<Error> AsioTcpTransport::ActiveCore::Connect(
    Resolver::iterator iterator) {
  auto ref = std::static_pointer_cast<ActiveCore>(shared_from_this());

  auto [error, connected_iterator] = co_await boost::asio::async_connect(
      io_object_, iterator, boost::asio::as_tuple(boost::asio::use_awaitable));

  DFAKE_SCOPED_RECURSIVE_LOCK(mutex_);

  if (closed_) {
    co_return ERR_ABORTED;
  }

  if (error) {
    if (error != boost::asio::error::operation_aborted) {
      logger_->Write(LogSeverity::Warning, "Connect error");
      ProcessError(error);
    }
    co_return error;
  }

  logger_->WriteF(LogSeverity::Normal, "Connected to %s",
                  connected_iterator->host_name().c_str());

  connected_ = true;

  co_return OK;
}

void AsioTcpTransport::ActiveCore::Cleanup() {
  DFAKE_SCOPED_RECURSIVE_LOCK(mutex_);

  assert(closed_);

  logger_->Write(LogSeverity::Normal, "Cleanup");

  connected_ = false;

  resolver_.cancel();

  boost::system::error_code ec;
  io_object_.cancel(ec);
  io_object_.close(ec);
}

// AsioTcpTransport::PassiveCore

class AsioTcpTransport::PassiveCore final
    : public Core,
      public std::enable_shared_from_this<PassiveCore> {
 public:
  // The executor must be a sequential one.
  PassiveCore(const Executor& executor,
              std::shared_ptr<const Logger> logger,
              const std::string& host,
              const std::string& service);

  int GetLocalPort() const;

  // Core
  virtual Executor GetExecutor() override { return acceptor_.get_executor(); }
  virtual bool IsConnected() const override { return connected_; }
  virtual awaitable<Error> Open() override;
  virtual void Close() override;

  [[nodiscard]] virtual awaitable<ErrorOr<std::unique_ptr<Transport>>> Accept()
      override;

  [[nodiscard]] virtual awaitable<ErrorOr<size_t>> Read(
      std::span<char> data) override;

  [[nodiscard]] virtual awaitable<ErrorOr<size_t>> Write(
      std::span<const char> data) override;

 private:
  using Socket = boost::asio::ip::tcp::socket;
  using Resolver = boost::asio::ip::tcp::resolver;

  [[nodiscard]] awaitable<Error> ResolveAndStartAccepting();
  [[nodiscard]] boost::system::error_code Bind(Resolver::iterator iterator);
  [[nodiscard]] awaitable<Error> StartAccepting();

  void ProcessError(const boost::system::error_code& ec);

  DFAKE_MUTEX(mutex_);

  const std::shared_ptr<const Logger> logger_;
  std::string host_;
  std::string service_;

  Resolver resolver_;
  boost::asio::ip::tcp::acceptor acceptor_;

  bool connected_ = false;
  bool closed_ = false;
};

AsioTcpTransport::PassiveCore::PassiveCore(const Executor& executor,
                                           std::shared_ptr<const Logger> logger,
                                           const std::string& host,
                                           const std::string& service)
    : logger_{std::move(logger)},
      host_{host},
      service_{service},
      resolver_{executor},
      acceptor_{executor} {}

int AsioTcpTransport::PassiveCore::GetLocalPort() const {
  DFAKE_SCOPED_RECURSIVE_LOCK(mutex_);
  return acceptor_.local_endpoint().port();
}

awaitable<Error> AsioTcpTransport::PassiveCore::Open() {
  auto ref = shared_from_this();

  logger_->WriteF(LogSeverity::Normal, "Open");

  return ResolveAndStartAccepting();
}

awaitable<Error> AsioTcpTransport::PassiveCore::ResolveAndStartAccepting() {
  logger_->WriteF(LogSeverity::Normal, "Start DNS resolution to %s:%s",
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
      logger_->Write(LogSeverity::Warning, "DNS resolution error");
      ProcessError(error);
    }
    co_return error;
  }

  logger_->Write(LogSeverity::Normal, "DNS resolution completed");

  if (auto ec = Bind(std::move(iterator)); ec) {
    logger_->Write(LogSeverity::Warning, "Bind error");
    ProcessError(ec);
    co_return error;
  }

  logger_->Write(LogSeverity::Normal, "Bind completed");

  connected_ = true;

  boost::asio::co_spawn(acceptor_.get_executor(),
                        std::bind_front(&PassiveCore::StartAccepting, ref),
                        boost::asio::detached);

  co_return OK;
}

boost::system::error_code AsioTcpTransport::PassiveCore::Bind(
    Resolver::iterator iterator) {
  DFAKE_SCOPED_RECURSIVE_LOCK(mutex_);

  logger_->Write(LogSeverity::Normal, "Bind");

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

void AsioTcpTransport::PassiveCore::Close() {
  boost::asio::dispatch(acceptor_.get_executor(),
                        [this, ref = shared_from_this()] {
                          DFAKE_SCOPED_RECURSIVE_LOCK(mutex_);

                          if (closed_) {
                            return;
                          }

                          logger_->Write(LogSeverity::Normal, "Close");

                          closed_ = true;
                          connected_ = false;
                          acceptor_.close();
                        });
}

awaitable<ErrorOr<std::unique_ptr<Transport>>>
AsioTcpTransport::PassiveCore::Accept() {
  co_return ERR_ACCESS_DENIED;
}

awaitable<ErrorOr<size_t>> AsioTcpTransport::PassiveCore::Read(
    std::span<char> data) {
  co_return ERR_ACCESS_DENIED;
}

awaitable<ErrorOr<size_t>> AsioTcpTransport::PassiveCore::Write(
    std::span<const char> data) {
  co_return ERR_ACCESS_DENIED;
}

awaitable<Error> AsioTcpTransport::PassiveCore::StartAccepting() {
  auto ref = shared_from_this();

  while (!closed_) {
    // TODO: Use different executor.
    auto [error, peer] = co_await acceptor_.async_accept(
        boost::asio::as_tuple(boost::asio::use_awaitable));

    assert(peer.get_executor() == acceptor_.get_executor());

    if (closed_) {
      co_return ERR_ABORTED;
    }

    // TODO: Log connection information.
    logger_->Write(LogSeverity::Normal, "Accept incoming connection");

    if (error) {
      if (error != boost::asio::error::operation_aborted) {
        logger_->Write(LogSeverity::Warning, "Accept connection error");
        ProcessError(error);
      }
      co_return error;
    }

    logger_->Write(LogSeverity::Normal, "Connection accepted");

    /*if (handlers_.on_accept) {
      auto accepted_transport =
          std::make_unique<AsioTcpTransport>(logger_, std::move(peer));

      handlers_.on_accept(std::move(accepted_transport));
    }*/
  }

  co_return OK;
}

void AsioTcpTransport::PassiveCore::ProcessError(
    const boost::system::error_code& ec) {
  DFAKE_SCOPED_RECURSIVE_LOCK(mutex_);

  if (closed_) {
    return;
  }

  if (ec != OK) {
    logger_->WriteF(LogSeverity::Warning, "Error: %s",
                    ErrorToShortString(ec).c_str());
  } else {
    logger_->WriteF(LogSeverity::Normal, "Graceful close");
  }

  connected_ = false;
  closed_ = true;
}

// AsioTcpTransport

AsioTcpTransport::AsioTcpTransport(const Executor& executor,
                                   std::shared_ptr<const Logger> logger,
                                   std::string host,
                                   std::string service,
                                   bool active)
    : type_{active ? Type::ACTIVE : Type::PASSIVE} {
  if (active) {
    core_ = std::make_shared<ActiveCore>(executor, std::move(logger),
                                         std::move(host), std::move(service));
  } else {
    core_ = std::make_shared<PassiveCore>(executor, std::move(logger),
                                          std::move(host), std::move(service));
  }
}

AsioTcpTransport::AsioTcpTransport(std::shared_ptr<const Logger> logger,
                                   boost::asio::ip::tcp::socket socket)
    : type_{Type::ACCEPTED} {
  core_ = std::make_shared<ActiveCore>(std::move(logger), std::move(socket));
}

AsioTcpTransport::~AsioTcpTransport() {
  // The base class closes the core on destruction.
}

awaitable<Error> AsioTcpTransport::Open() {
  co_return co_await core_->Open();
}

awaitable<ErrorOr<std::unique_ptr<Transport>>> AsioTcpTransport::Accept() {
  co_return co_await core_->Accept();
}

int AsioTcpTransport::GetLocalPort() const {
  return core_ && type_ == Type::PASSIVE
             ? std::static_pointer_cast<PassiveCore>(core_)->GetLocalPort()
             : 0;
}

std::string AsioTcpTransport::GetName() const {
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

}  // namespace net
