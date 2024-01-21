#include "net/tcp_transport.h"

#include "net/logger.h"
#include "net/transport_util.h"

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

  ActiveCore(std::shared_ptr<const Logger> logger, Socket socket);

  // Core
  virtual promise<void> Open(const Handlers& handlers) override;

 private:
  using Resolver = boost::asio::ip::tcp::resolver;

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

promise<void> AsioTcpTransport::ActiveCore::Open(const Handlers& handlers) {
  return DispatchPromise(
      io_object_.get_executor(), [this, ref = shared_from_this(), handlers] {
        DFAKE_SCOPED_RECURSIVE_LOCK(mutex_);

        if (connected_) {
          handlers_ = handlers;
          StartReading();
          return make_resolved_promise();
        }

        auto [p, promise_handlers] = MakePromiseHandlers(handlers);
        handlers_ = std::move(promise_handlers);

        logger_->WriteF(LogSeverity::Normal, "Start DNS resolution to %s:%s",
                        host_.c_str(), service_.c_str());

        resolver_.async_resolve(
            host_, service_,
            [this, ref = shared_from_this()](
                const boost::system::error_code& error,
                Resolver::iterator iterator) {
              DFAKE_SCOPED_RECURSIVE_LOCK(mutex_);

              if (closed_)
                return;

              if (error) {
                if (error != boost::asio::error::operation_aborted) {
                  logger_->Write(LogSeverity::Warning, "DNS resolution error");
                  ProcessError(MapSystemError(error.value()));
                }
                return;
              }

              logger_->Write(LogSeverity::Normal, "DNS resolution completed");

              boost::asio::async_connect(
                  io_object_, iterator,
                  [this, ref = shared_from_this()](
                      const boost::system::error_code& error,
                      Resolver::iterator iterator) {
                    DFAKE_SCOPED_RECURSIVE_LOCK(mutex_);

                    if (closed_)
                      return;

                    if (error) {
                      if (error != boost::asio::error::operation_aborted) {
                        logger_->Write(LogSeverity::Warning, "Connect error");
                        ProcessError(MapSystemError(error.value()));
                      }
                      return;
                    }

                    logger_->WriteF(LogSeverity::Normal, "Connected to %s",
                                    iterator->host_name().c_str());

                    connected_ = true;

                    if (handlers_.on_open) {
                      handlers_.on_open();
                    }

                    StartReading();
                  });
            });

        return p;
      });
}

void AsioTcpTransport::ActiveCore::Cleanup() {
  boost::asio::dispatch(io_object_.get_executor(),
                        [this, ref = shared_from_this()] {
                          DFAKE_SCOPED_RECURSIVE_LOCK(mutex_);

                          assert(closed_);

                          connected_ = false;

                          resolver_.cancel();

                          boost::system::error_code ec;
                          io_object_.cancel(ec);
                          io_object_.close(ec);
                        });
}

// AsioTcpTransport::PassiveCore

class AsioTcpTransport::PassiveCore final
    : public Core,
      public std::enable_shared_from_this<PassiveCore> {
 public:
  PassiveCore(const boost::asio::any_io_executor& executor,
              std::shared_ptr<const Logger> logger,
              const std::string& host,
              const std::string& service);

  int GetLocalPort() const;

  // Core
  virtual bool IsConnected() const override { return connected_; }
  virtual promise<void> Open(const Handlers& handlers) override;
  virtual void Close() override;
  virtual int Read(std::span<char> data) override;
  virtual promise<size_t> Write(std::span<const char> data) override;

 private:
  using Socket = boost::asio::ip::tcp::socket;
  using Resolver = boost::asio::ip::tcp::resolver;

  void StartAccepting();

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

AsioTcpTransport::PassiveCore::PassiveCore(
    const boost::asio::any_io_executor& executor,
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

promise<void> AsioTcpTransport::PassiveCore::Open(const Handlers& handlers) {
  return DispatchPromise(
      acceptor_.get_executor(), [this, ref = shared_from_this(), handlers] {
        DFAKE_SCOPED_RECURSIVE_LOCK(mutex_);

        auto [p, promise_handlers] = MakePromiseHandlers(handlers);
        handlers_ = std::move(promise_handlers);

        logger_->WriteF(LogSeverity::Normal, "Start DNS resolution to %s:%s",
                        host_.c_str(), service_.c_str());

        Resolver::query query{host_, service_};
        resolver_.async_resolve(
            query, [this, ref = shared_from_this()](
                       const boost::system::error_code& error,
                       Resolver::iterator iterator) {
              DFAKE_SCOPED_RECURSIVE_LOCK(mutex_);

              if (closed_)
                return;

              if (error) {
                if (error != boost::asio::error::operation_aborted) {
                  logger_->Write(LogSeverity::Warning, "DNS resolution error");
                  ProcessError(error);
                }
                return;
              }

              logger_->Write(LogSeverity::Normal, "DNS resolution completed");

              boost::system::error_code ec = boost::asio::error::fault;
              for (Resolver::iterator end; iterator != end; ++iterator) {
                acceptor_.open(iterator->endpoint().protocol(), ec);
                if (ec)
                  continue;
                acceptor_.set_option(Socket::reuse_address{true}, ec);
                acceptor_.bind(iterator->endpoint(), ec);
                if (!ec)
                  acceptor_.listen(Socket::max_listen_connections, ec);
                if (!ec)
                  break;
                acceptor_.close();
              }

              if (ec) {
                logger_->Write(LogSeverity::Warning, "Bind error");
                ProcessError(ec);
                return;
              }

              logger_->Write(LogSeverity::Normal, "Bind completed");

              connected_ = true;
              if (handlers_.on_open)
                handlers_.on_open();

              StartAccepting();
            });

        return p;
      });
}

void AsioTcpTransport::PassiveCore::Close() {
  boost::asio::dispatch(acceptor_.get_executor(),
                        [this, ref = shared_from_this()] {
                          DFAKE_SCOPED_RECURSIVE_LOCK(mutex_);

                          if (closed_)
                            return;

                          logger_->Write(LogSeverity::Normal, "Close");

                          closed_ = true;
                          connected_ = false;
                          acceptor_.close();
                        });
}

int AsioTcpTransport::PassiveCore::Read(std::span<char> data) {
  return ERR_ACCESS_DENIED;
}

promise<size_t> AsioTcpTransport::PassiveCore::Write(
    std::span<const char> data) {
  return make_error_promise<size_t>(ERR_ACCESS_DENIED);
}

void AsioTcpTransport::PassiveCore::StartAccepting() {
  boost::asio::dispatch(
      acceptor_.get_executor(), [this, ref = shared_from_this()] {
        DFAKE_SCOPED_RECURSIVE_LOCK(mutex_);

        if (closed_)
          return;

        acceptor_.async_accept([this, outer_ref = shared_from_this()](
                                   const boost::system::error_code& error,
                                   Socket peer) {
          DFAKE_SCOPED_RECURSIVE_LOCK(mutex_);

          assert(peer.get_executor() == acceptor_.get_executor());

          if (closed_)
            return;

          if (error) {
            if (error != boost::asio::error::operation_aborted) {
              logger_->Write(LogSeverity::Warning, "Accept connection error");
              ProcessError(error);
            }
            return;
          }

          logger_->Write(LogSeverity::Normal, "Connection accepted");

          if (handlers_.on_accept) {
            auto accepted_transport =
                std::make_unique<AsioTcpTransport>(logger_, std::move(peer));
            handlers_.on_accept(std::move(accepted_transport));
          }

          StartAccepting();
        });
      });
}

void AsioTcpTransport::PassiveCore::ProcessError(
    const boost::system::error_code& ec) {
  boost::asio::dispatch(acceptor_.get_executor(),
                        [this, ref = shared_from_this(), ec] {
                          DFAKE_SCOPED_RECURSIVE_LOCK(mutex_);

                          connected_ = false;
                          closed_ = true;

                          if (handlers_.on_close) {
                            handlers_.on_close(MapSystemError(ec.value()));
                          }
                        });
}

// AsioTcpTransport

AsioTcpTransport::AsioTcpTransport(const boost::asio::any_io_executor& executor,
                                   std::shared_ptr<const Logger> logger,
                                   std::string host,
                                   std::string service,
                                   bool active)
    : executor_{executor},
      logger_(std::move(logger)),
      host_{std::move(host)},
      service_{std::move(service)},
      active_{active} {}

AsioTcpTransport::AsioTcpTransport(std::shared_ptr<const Logger> logger,
                                   boost::asio::ip::tcp::socket socket)
    : executor_{socket.get_executor()}, logger_{std::move(logger)} {
  core_ = std::make_shared<ActiveCore>(logger_, std::move(socket));
}

AsioTcpTransport::~AsioTcpTransport() {
  if (core_)
    core_->Close();
}

promise<void> AsioTcpTransport::Open(const Handlers& handlers) {
  if (!core_) {
    core_ = active_
                ? std::static_pointer_cast<Core>(std::make_shared<ActiveCore>(
                      executor_, logger_, host_, service_))
                : std::static_pointer_cast<Core>(std::make_shared<PassiveCore>(
                      executor_, logger_, host_, service_));
  }

  return core_->Open(handlers);
}

int AsioTcpTransport::GetLocalPort() const {
  return core_ && !active_
             ? std::static_pointer_cast<PassiveCore>(core_)->GetLocalPort()
             : 0;
}

std::string AsioTcpTransport::GetName() const {
  return "TCP";
}

}  // namespace net
