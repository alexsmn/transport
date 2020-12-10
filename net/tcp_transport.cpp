#include "tcp_transport.h"

namespace net {

// AsioTcpTransport::ActiveCore

class AsioTcpTransport::ActiveCore final
    : public IoCore<boost::asio::ip::tcp::socket> {
 public:
  using Socket = boost::asio::ip::tcp::socket;

  ActiveCore(boost::asio::io_context& io_context,
             const std::string& host,
             const std::string& service);
  ActiveCore(boost::asio::io_context& io_context, Socket socket);

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
                                         const std::string& host,
                                         const std::string& service)
    : IoCore{io_context},
      resolver_{io_context},
      host_{host},
      service_{service} {}

AsioTcpTransport::ActiveCore::ActiveCore(boost::asio::io_context& io_context,
                                         Socket socket)
    : IoCore{io_context}, resolver_{io_context} {
  io_object_ = std::move(socket);
  connected_ = true;
}

void AsioTcpTransport::ActiveCore::Open(Delegate& delegate) {
  delegate_ = &delegate;

  if (connected_) {
    StartReading();
    return;
  }

  Resolver::query query{host_, service_};
  resolver_.async_resolve(query, [this, ref = shared_from_this()](
                                     const boost::system::error_code& error,
                                     Resolver::iterator iterator) {
    if (closed_)
      return;

    if (error) {
      if (error != boost::asio::error::operation_aborted)
        ProcessError(net::MapSystemError(error.value()));
      return;
    }

    boost::asio::async_connect(
        io_object_, iterator,
        [this, ref = shared_from_this()](const boost::system::error_code& error,
                                         Resolver::iterator iterator) {
          if (closed_)
            return;

          if (error) {
            if (error != boost::asio::error::operation_aborted)
              ProcessError(net::MapSystemError(error.value()));
            return;
          }

          connected_ = true;
          delegate_->OnTransportOpened();

          StartReading();
        });
  });
}

void AsioTcpTransport::ActiveCore::Cleanup() {
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

  boost::asio::io_context& io_context_;
  std::string host_;
  std::string service_;
  Delegate* delegate_ = nullptr;

  Resolver resolver_;
  boost::asio::ip::tcp::acceptor acceptor_;

  bool connected_ = false;
  bool closed_ = false;
};

AsioTcpTransport::PassiveCore::PassiveCore(boost::asio::io_context& io_context,
                                           const std::string& host,
                                           const std::string& service)
    : io_context_{io_context},
      resolver_{io_context},
      acceptor_{io_context},
      host_{host},
      service_{service} {}

void AsioTcpTransport::PassiveCore::Open(Delegate& delegate) {
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
      ProcessError(ec);
      return;
    }

    connected_ = true;
    delegate_->OnTransportOpened();

    StartAccepting();
  });
}

void AsioTcpTransport::PassiveCore::Close() {
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
  if (closed_)
    return;

  acceptor_.async_accept(
      [this, ref = shared_from_this()](const boost::system::error_code& error,
                                       Socket peer) {
        if (closed_)
          return;

        if (error) {
          ProcessError(error);
          return;
        }

        delegate_->OnTransportAccepted(
            std::make_unique<AsioTcpTransport>(io_context_, std::move(peer)));

        StartAccepting();
      });
}

void AsioTcpTransport::PassiveCore::ProcessError(
    const boost::system::error_code& ec) {
  connected_ = false;
  closed_ = true;
  delegate_->OnTransportClosed(net::MapSystemError(ec.value()));
}

// AsioTcpTransport

AsioTcpTransport::AsioTcpTransport(boost::asio::io_context& io_context)
    : io_context_{io_context} {}

AsioTcpTransport::AsioTcpTransport(boost::asio::io_context& io_context,
                                   boost::asio::ip::tcp::socket socket)
    : io_context_{io_context} {
  core_ = std::make_shared<ActiveCore>(io_context_, std::move(socket));
}

AsioTcpTransport::~AsioTcpTransport() {
  if (core_)
    core_->Close();
}

Error AsioTcpTransport::Open(Transport::Delegate& delegate) {
  if (!core_) {
    if (active)
      core_ = std::make_shared<ActiveCore>(io_context_, host, service);
    else
      core_ = std::make_shared<PassiveCore>(io_context_, host, service);
  }
  core_->Open(delegate);
  return net::OK;
}

std::string AsioTcpTransport::GetName() const {
  return "TCP";
}

}  // namespace net
