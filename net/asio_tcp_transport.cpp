#include "asio_tcp_transport.h"

namespace net {

// AsioTcpTransport::Core

class AsioTcpTransport::Core : public std::enable_shared_from_this<Core> {
 public:
  Core(boost::asio::io_context& io_context);

  bool connected() const { return connected_; }

  void Open(const std::string& host,
            const std::string& service,
            Delegate& delegate);
  void Close();

  int Read(void* data, size_t len);
  int Write(const void* data, size_t len);

 private:
  using Resolver = boost::asio::ip::tcp::resolver;
  using Socket = boost::asio::ip::tcp::socket;

  void StartReading();

  void ProcessError(const boost::system::error_code& ec);

  boost::asio::io_context& io_context_;
  Resolver resolver_;
  Socket socket_;

  std::string service_;
  Delegate* delegate_ = nullptr;

  bool connected_ = false;

  boost::circular_buffer<char> read_buffer_{1024};

  bool reading_ = false;
  std::vector<char> reading_buffer_;

  bool closed_ = false;
};

AsioTcpTransport::Core::Core(boost::asio::io_context& io_context)
    : io_context_{io_context}, resolver_{io_context}, socket_{io_context} {}

void AsioTcpTransport::Core::Open(const std::string& host,
                                  const std::string& service,
                                  Delegate& delegate) {
  service_ = service;
  delegate_ = &delegate;

  Resolver::query query{host, service_};
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

    boost::asio::async_connect(
        socket_, iterator,
        [this, ref = shared_from_this()](const boost::system::error_code& error,
                                         Resolver::iterator iterator) {
          if (closed_)
            return;

          if (error) {
            if (error != boost::asio::error::operation_aborted)
              ProcessError(error);
            return;
          }

          connected_ = true;
          delegate_->OnTransportOpened();

          StartReading();
        });
  });
}

void AsioTcpTransport::Core::Close() {
  closed_ = true;
  connected_ = false;
  resolver_.cancel();
  boost::system::error_code ec;
  socket_.cancel(ec);
}

int AsioTcpTransport::Core::Read(void* data, size_t len) {
  size_t count = std::min(len, read_buffer_.size());
  std::copy(read_buffer_.begin(), read_buffer_.begin() + count,
            reinterpret_cast<char*>(data));
  read_buffer_.erase_begin(count);
  StartReading();
  return count;
}

int AsioTcpTransport::Core::Write(const void* data, size_t len) {
  boost::system::error_code ec;
  auto bytes_written = socket_.write_some(boost::asio::buffer(data, len), ec);
  return bytes_written ? bytes_written : net::MapSystemError(ec.value());
}

void AsioTcpTransport::Core::StartReading() {
  if (reading_)
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
            ProcessError(ec);
          return;
        }

        read_buffer_.insert(read_buffer_.end(), reading_buffer_.begin(),
                            reading_buffer_.begin() + bytes_transferred);
        delegate_->OnTransportDataReceived();

        StartReading();
      });
}

void AsioTcpTransport::Core::ProcessError(const boost::system::error_code& ec) {
  assert(connected_);
  connected_ = false;
  if (delegate_)
    delegate_->OnTransportClosed(net::MapSystemError(ec.value()));
}

// AsioTcpTransport

AsioTcpTransport::AsioTcpTransport(boost::asio::io_context& io_context)
    : core_{std::make_shared<Core>(io_context)} {}

AsioTcpTransport::~AsioTcpTransport() {}

Error AsioTcpTransport::Open() {
  core_->Open(host, service, *delegate_);
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
  return core_->connected();
}

}  // namespace net
