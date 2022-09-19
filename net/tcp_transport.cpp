#include "tcp_transport.h"

#include "logger.h"

namespace net {

// AsioTcpTransport::ActiveCore

class AsioTcpTransport::ActiveCore final
    : public IoCore<boost::asio::ip::tcp::socket> {
 public:
  using Socket = boost::asio::ip::tcp::socket;

  ActiveCore(boost::asio::io_context& io_context,
             std::shared_ptr<const Logger> logger,
             const std::string& host,
             const std::string& service);
  ActiveCore(boost::asio::io_context& io_context,
             std::shared_ptr<const Logger> logger,
             Socket socket);

  // Core
  virtual void Open(Delegate& delegate) override;

 private:
  using Resolver = boost::asio::ip::tcp::resolver;

  virtual void Cleanup() override;

  std::string host_;
  std::string service_;

  Resolver resolver_;
};

AsioTcpTransport::ActiveCore::ActiveCore(boost::asio::io_context& io_context,
                                         std::shared_ptr<const Logger> logger,
                                         const std::string& host,
                                         const std::string& service)
    : IoCore{io_context, std::move(logger)},
      resolver_{io_context},
      host_{host},
      service_{service} {}

AsioTcpTransport::ActiveCore::ActiveCore(boost::asio::io_context& io_context,
                                         std::shared_ptr<const Logger> logger,
                                         Socket socket)
    : IoCore{io_context, std::move(logger)}, resolver_{io_context} {
  io_object_ = std::move(socket);
  connected_ = true;
}

void AsioTcpTransport::ActiveCore::Open(Delegate& delegate) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);

  delegate_ = &delegate;

  if (connected_) {
    StartReading();
    return;
  }

  logger_->WriteF(LogSeverity::Normal, "Start DNS resolution to %s:%s",
                  host_.c_str(), service_.c_str());

  Resolver::query query{host_, service_};
  resolver_.async_resolve(query, [this, ref = shared_from_this()](
                                     const boost::system::error_code& error,
                                     Resolver::iterator iterator) {
    DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);

    if (closed_)
      return;

    if (error) {
      if (error != boost::asio::error::operation_aborted) {
        logger_->Write(LogSeverity::Warning, "DNS resolution error");
        ProcessError(net::MapSystemError(error.value()));
      }
      return;
    }

    logger_->Write(LogSeverity::Normal, "DNS resolution completed");

    boost::asio::async_connect(
        io_object_, iterator,
        [this, ref = shared_from_this()](const boost::system::error_code& error,
                                         Resolver::iterator iterator) {
          DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);

          if (closed_)
            return;

          if (error) {
            if (error != boost::asio::error::operation_aborted) {
              logger_->Write(LogSeverity::Warning, "Connect error");
              ProcessError(net::MapSystemError(error.value()));
            }
            return;
          }

          logger_->WriteF(LogSeverity::Normal, "Connected to %s",
                          iterator->host_name().c_str());

          connected_ = true;
          delegate_->OnTransportOpened();

          StartReading();
        });
  });
}

void AsioTcpTransport::ActiveCore::Cleanup() {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);

  assert(closed_);

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
  PassiveCore(boost::asio::io_context& io_context,
              std::shared_ptr<const Logger> logger,
              const std::string& host,
              const std::string& service);

  // Core
  virtual bool IsConnected() const override { return connected_; }
  virtual void Open(Delegate& delegate) override;
  virtual void Close() override;
  virtual int Read(void* data, size_t len) override;
  virtual int Write(const void* data, size_t len) override;

 private:
  using Socket = boost::asio::ip::tcp::socket;
  using Resolver = boost::asio::ip::tcp::resolver;

  void StartAccepting();

  void ProcessError(const boost::system::error_code& ec);

  THREAD_CHECKER(thread_checker_);

  boost::asio::io_context& io_context_;
  const std::shared_ptr<const Logger> logger_;
  std::string host_;
  std::string service_;
  Delegate* delegate_ = nullptr;

  Resolver resolver_;
  boost::asio::ip::tcp::acceptor acceptor_;

  bool connected_ = false;
  bool closed_ = false;
};

AsioTcpTransport::PassiveCore::PassiveCore(boost::asio::io_context& io_context,
                                           std::shared_ptr<const Logger> logger,
                                           const std::string& host,
                                           const std::string& service)
    : io_context_{io_context},
      logger_{std::move(logger)},
      resolver_{io_context},
      acceptor_{io_context},
      host_{host},
      service_{service} {}

void AsioTcpTransport::PassiveCore::Open(Delegate& delegate) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);

  delegate_ = &delegate;

  logger_->WriteF(LogSeverity::Normal, "Start DNS resolution to %s:%s",
                  host_.c_str(), service_.c_str());

  Resolver::query query{host_, service_};
  resolver_.async_resolve(query, [this, ref = shared_from_this()](
                                     const boost::system::error_code& error,
                                     Resolver::iterator iterator) {
    DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);

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
    delegate_->OnTransportOpened();

    StartAccepting();
  });
}

void AsioTcpTransport::PassiveCore::Close() {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);

  logger_->Write(LogSeverity::Normal, "Close");

  closed_ = true;
  connected_ = false;
  acceptor_.close();
}

int AsioTcpTransport::PassiveCore::Read(void* data, size_t len) {
  return net::ERR_ACCESS_DENIED;
}

int AsioTcpTransport::PassiveCore::Write(const void* data, size_t len) {
  return net::ERR_ACCESS_DENIED;
}

void AsioTcpTransport::PassiveCore::StartAccepting() {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);

  if (closed_)
    return;

  acceptor_.async_accept(
      [this, ref = shared_from_this()](const boost::system::error_code& error,
                                       Socket peer) {
        DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);

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

        auto accepted_transport = std::make_unique<AsioTcpTransport>(
            io_context_, logger_, std::move(peer));
        delegate_->OnTransportAccepted(std::move(accepted_transport));

        StartAccepting();
      });
}

void AsioTcpTransport::PassiveCore::ProcessError(
    const boost::system::error_code& ec) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);

  connected_ = false;
  closed_ = true;
  delegate_->OnTransportClosed(net::MapSystemError(ec.value()));
}

// AsioTcpTransport

AsioTcpTransport::AsioTcpTransport(boost::asio::io_context& io_context,
                                   std::shared_ptr<const Logger> logger)
    : io_context_{io_context}, logger_(std::move(logger)) {}

AsioTcpTransport::AsioTcpTransport(boost::asio::io_context& io_context,
                                   std::shared_ptr<const Logger> logger,
                                   boost::asio::ip::tcp::socket socket)
    : io_context_{io_context}, logger_(std::move(logger)) {
  core_ = std::make_shared<ActiveCore>(io_context_, logger_, std::move(socket));
}

AsioTcpTransport::~AsioTcpTransport() {
  if (core_)
    core_->Close();
}

Error AsioTcpTransport::Open(Transport::Delegate& delegate) {
  if (!core_) {
    if (active)
      core_ = std::make_shared<ActiveCore>(io_context_, logger_, host, service);
    else
      core_ =
          std::make_shared<PassiveCore>(io_context_, logger_, host, service);
  }
  core_->Open(delegate);
  return net::OK;
}

std::string AsioTcpTransport::GetName() const {
  return "TCP";
}

}  // namespace net
