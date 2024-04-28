#include "net/tcp_transport.h"

#include "net/logger.h"
#include "net/net_exception.h"

namespace net {

// AsioTcpTransport::ActiveCore

class AsioTcpTransport::ActiveCore final
    : public IoCore<boost::asio::ip::tcp::socket> {
 public:
  using Socket = boost::asio::ip::tcp::socket;

  ActiveCore(const boost::asio::any_io_executor& executor,
             std::shared_ptr<const Logger> logger,
             const std::string& host,
             const std::string& service);

  // A constructor for a socket accepted by a passive TCP transport.
  // Uses the executor of the socket.
  ActiveCore(std::shared_ptr<const Logger> logger, Socket socket);

  // Core
  virtual awaitable<void> Open(Handlers handlers) override;

 private:
  using Resolver = boost::asio::ip::tcp::resolver;

  awaitable<void> StartResolving();
  awaitable<void> StartConnecting(Resolver::iterator iterator);

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

awaitable<void> AsioTcpTransport::ActiveCore::Open(
    Handlers handlers) {
  auto ref = std::static_pointer_cast<ActiveCore>(shared_from_this());

  if (connected_) {
    handlers_ = std::move(handlers);

    boost::asio::co_spawn(io_object_.get_executor(),
                          std::bind_front(&ActiveCore::StartReading, ref),
                          boost::asio::detached);

    co_return;
  }

  logger_->WriteF(LogSeverity::Normal, "Open");

  handlers_ = std::move(handlers);

  co_await StartResolving();
}

awaitable<void> AsioTcpTransport::ActiveCore::StartResolving() {
  auto ref = shared_from_this();

  logger_->WriteF(LogSeverity::Normal, "Start DNS resolution to %s:%s",
                  host_.c_str(), service_.c_str());

  auto [error, iterator] = co_await resolver_.async_resolve(
      host_, service_, boost::asio::as_tuple(boost::asio::use_awaitable));

  if (closed_) {
    co_return;
  }

  if (error) {
    if (error != boost::asio::error::operation_aborted) {
      logger_->Write(LogSeverity::Warning, "DNS resolution error");
      ProcessError(MapSystemError(error.value()));
    }
    co_return;
  }

  logger_->Write(LogSeverity::Normal, "DNS resolution completed");

  co_await StartConnecting(std::move(iterator));
}

awaitable<void> AsioTcpTransport::ActiveCore::StartConnecting(
    Resolver::iterator iterator) {
  auto ref = std::static_pointer_cast<ActiveCore>(shared_from_this());

  auto [error, connected_iterator] = co_await boost::asio::async_connect(
      io_object_, iterator, boost::asio::as_tuple(boost::asio::use_awaitable));

  DFAKE_SCOPED_RECURSIVE_LOCK(mutex_);

  if (closed_) {
    co_return;
  }

  if (error) {
    if (error != boost::asio::error::operation_aborted) {
      logger_->Write(LogSeverity::Warning, "Connect error");
      ProcessError(MapSystemError(error.value()));
    }
    co_return;
  }

  logger_->WriteF(LogSeverity::Normal, "Connected to %s",
                  connected_iterator->host_name().c_str());

  connected_ = true;

  if (handlers_.on_open) {
    handlers_.on_open();
  }

  boost::asio::co_spawn(io_object_.get_executor(),
                        std::bind_front(&ActiveCore::StartReading, ref),
                        boost::asio::detached);
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
  virtual awaitable<void> Open(Handlers handlers) override;
  virtual void Close() override;
  virtual int Read(std::span<char> data) override;

  [[nodiscard]] virtual awaitable<size_t> Write(
      std::vector<char> data) override;

 private:
  using Socket = boost::asio::ip::tcp::socket;
  using Resolver = boost::asio::ip::tcp::resolver;

  awaitable<void> StartResolving();
  boost::system::error_code Bind(Resolver::iterator iterator);
  awaitable<void> StartAccepting();

  void ProcessError(const boost::system::error_code& ec);

  DFAKE_MUTEX(mutex_);

  const std::shared_ptr<const Logger> logger_;
  std::string host_;
  std::string service_;
  Handlers handlers_;

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

awaitable<void> AsioTcpTransport::PassiveCore::Open(
    Handlers handlers) {
  auto ref = shared_from_this();

  logger_->WriteF(LogSeverity::Normal, "Open");

  handlers_ = std::move(handlers);

  co_await StartResolving();
}

awaitable<void> AsioTcpTransport::PassiveCore::StartResolving() {
  logger_->WriteF(LogSeverity::Normal, "Start DNS resolution to %s:%s",
                  host_.c_str(), service_.c_str());

  auto ref = shared_from_this();

  auto [error, iterator] = co_await resolver_.async_resolve(
      /*query=*/{host_, service_},
      boost::asio::as_tuple(boost::asio::use_awaitable));

  if (closed_) {
    co_return;
  }

  if (error) {
    if (error != boost::asio::error::operation_aborted) {
      logger_->Write(LogSeverity::Warning, "DNS resolution error");
      ProcessError(error);
    }
    co_return;
  }

  logger_->Write(LogSeverity::Normal, "DNS resolution completed");

  if (auto ec = Bind(std::move(iterator)); ec) {
    logger_->Write(LogSeverity::Warning, "Bind error");
    ProcessError(ec);
    co_return;
  }

  logger_->Write(LogSeverity::Normal, "Bind completed");

  connected_ = true;

  if (handlers_.on_open) {
    handlers_.on_open();
  }

  boost::asio::co_spawn(acceptor_.get_executor(),
                        std::bind_front(&PassiveCore::StartAccepting, ref),
                        boost::asio::detached);
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

int AsioTcpTransport::PassiveCore::Read(std::span<char> data) {
  return ERR_ACCESS_DENIED;
}

awaitable<size_t> AsioTcpTransport::PassiveCore::Write(
    std::vector<char> data) {
  throw net_exception{ERR_ACCESS_DENIED};
}

awaitable<void> AsioTcpTransport::PassiveCore::StartAccepting() {
  auto ref = shared_from_this();

  while (!closed_) {
    // TODO: Use different executor.
    auto [error, peer] = co_await acceptor_.async_accept(
        boost::asio::as_tuple(boost::asio::use_awaitable));

    assert(peer.get_executor() == acceptor_.get_executor());

    if (closed_) {
      co_return;
    }

    // TODO: Log connection information.
    logger_->Write(LogSeverity::Normal, "Accept incoming connection");

    if (error) {
      if (error != boost::asio::error::operation_aborted) {
        logger_->Write(LogSeverity::Warning, "Accept connection error");
        ProcessError(error);
      }
      co_return;
    }

    logger_->Write(LogSeverity::Normal, "Connection accepted");

    if (handlers_.on_accept) {
      auto accepted_transport =
          std::make_unique<AsioTcpTransport>(logger_, std::move(peer));
      handlers_.on_accept(std::move(accepted_transport));
    }
  }
}

void AsioTcpTransport::PassiveCore::ProcessError(
    const boost::system::error_code& ec) {
  DFAKE_SCOPED_RECURSIVE_LOCK(mutex_);

  if (closed_) {
    return;
  }

  auto error = MapSystemError(ec.value());

  if (error != OK) {
    logger_->WriteF(LogSeverity::Warning, "Error: %s",
                    ErrorToShortString(error).c_str());
  } else {
    logger_->WriteF(LogSeverity::Normal, "Graceful close");
  }

  connected_ = false;
  closed_ = true;

  if (handlers_.on_close) {
    handlers_.on_close(error);
  }
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

awaitable<void> AsioTcpTransport::Open(Handlers handlers) {
  return core_->Open(std::move(handlers));
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
