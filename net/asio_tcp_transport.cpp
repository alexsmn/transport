#include "asio_tcp_transport.h"

namespace net {

// AsioTcpTransport::Core

class AsioTcpTransport::Core {
 public:
  virtual ~Core() {}

  virtual bool IsConnected() const = 0;

  virtual void Open(Delegate& delegate) = 0;
  virtual void Close() = 0;

  virtual int Read(void* data, size_t len) = 0;
  virtual int Write(const void* data, size_t len) = 0;
};

// AsioTcpTransport::ActiveCore

class AsioTcpTransport::ActiveCore final
    : public Core,
      public std::enable_shared_from_this<Core> {
 public:
  using Socket = boost::asio::ip::tcp::socket;

  ActiveCore(boost::asio::io_context& io_context,
             const std::string& host,
             const std::string& service);
  ActiveCore(boost::asio::io_context& io_context, Socket socket);

  // Core
  virtual bool IsConnected() const override { return connected_; }
  virtual void Open(Delegate& delegate) override;
  virtual void Close() override;
  virtual int Read(void* data, size_t len) override;
  virtual int Write(const void* data, size_t len) override;

 private:
  using Resolver = boost::asio::ip::tcp::resolver;

  void StartReading();

  void ProcessError(net::Error error);

  boost::asio::io_context& io_context_;
  std::string host_;
  std::string service_;
  Delegate* delegate_ = nullptr;

  Resolver resolver_;
  Socket socket_;

  bool connected_ = false;

  boost::circular_buffer<char> read_buffer_{1024};

  bool reading_ = false;
  std::vector<char> reading_buffer_;

  bool closed_ = false;
};

AsioTcpTransport::ActiveCore::ActiveCore(boost::asio::io_context& io_context,
                                         const std::string& host,
                                         const std::string& service)
    : io_context_{io_context},
      resolver_{io_context},
      socket_{io_context},
      host_{host},
      service_{service} {}

AsioTcpTransport::ActiveCore::ActiveCore(boost::asio::io_context& io_context,
                                         Socket socket)
    : io_context_{io_context},
      resolver_{io_context},
      socket_{std::move(socket)},
      connected_{true} {}

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
        socket_, iterator,
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

void AsioTcpTransport::ActiveCore::Close() {
  closed_ = true;
  connected_ = false;
  delegate_ = nullptr;
  resolver_.cancel();
  boost::system::error_code ec;
  socket_.cancel(ec);
}

int AsioTcpTransport::ActiveCore::Read(void* data, size_t len) {
  size_t count = std::min(len, read_buffer_.size());
  std::copy(read_buffer_.begin(), read_buffer_.begin() + count,
            reinterpret_cast<char*>(data));
  read_buffer_.erase_begin(count);
  StartReading();
  return count;
}

int AsioTcpTransport::ActiveCore::Write(const void* data, size_t len) {
  boost::system::error_code ec;
  auto bytes_written = socket_.write_some(boost::asio::buffer(data, len), ec);
  return bytes_written ? bytes_written : net::MapSystemError(ec.value());
}

void AsioTcpTransport::ActiveCore::StartReading() {
  if (closed_ || reading_)
    return;

  reading_buffer_.resize(read_buffer_.capacity() - read_buffer_.size());
  if (reading_buffer_.empty())
    return;

  reading_ = true;
  socket_.async_receive(
      boost::asio::buffer(reading_buffer_),
      [this, ref = shared_from_this()](const boost::system::error_code& ec,
                                       std::size_t bytes_transferred) {
        if (closed_)
          return;

        assert(reading_);
        reading_ = false;

        if (ec) {
          if (ec != boost::asio::error::operation_aborted)
            ProcessError(net::MapSystemError(ec.value()));
          return;
        }

        if (bytes_transferred == 0) {
          ProcessError(net::OK);
          return;
        }

        read_buffer_.insert(read_buffer_.end(), reading_buffer_.begin(),
                            reading_buffer_.begin() + bytes_transferred);
        delegate_->OnTransportDataReceived();

        StartReading();
      });
}

void AsioTcpTransport::ActiveCore::ProcessError(net::Error error) {
  assert(!closed_);
  connected_ = false;
  closed_ = true;
  delegate_->OnTransportClosed(error);
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
    : io_context_{io_context},
      core_{std::make_shared<ActiveCore>(io_context_, std::move(socket))} {}

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

void AsioTcpTransport::Close() {
  core_->Close();
  core_ = nullptr;
}

int AsioTcpTransport::Read(void* data, size_t len) {
  return core_ ? core_->Read(data, len) : net::ERR_FAILED;
}

int AsioTcpTransport::Write(const void* data, size_t len) {
  return core_ ? core_->Write(data, len) : net::ERR_FAILED;
}

std::string AsioTcpTransport::GetName() const {
  return "TCP";
}

bool AsioTcpTransport::IsMessageOriented() const {
  return false;
}

bool AsioTcpTransport::IsConnected() const {
  return core_ && core_->IsConnected();
}

}  // namespace net
