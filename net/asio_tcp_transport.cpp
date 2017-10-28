#include "asio_tcp_transport.h"

namespace net {

AsioTcpTransport::AsioTcpTransport(boost::asio::io_service& io_service)
    : resolver_{io_service},
      socket_{io_service} {
}

AsioTcpTransport::~AsioTcpTransport() {
}

Error AsioTcpTransport::Open() {
  Resolver::query query(host, service);
  resolver_.async_resolve(query, [this](const boost::system::error_code& error, Resolver::iterator iterator) {
    if (error) {
      if (error != boost::asio::error::operation_aborted)
        ProcessError(error);
      return;
    }

    boost::asio::async_connect(socket_, iterator, [this](const boost::system::error_code& error, Resolver::iterator iterator) {
      if (error) {
        if (error != boost::asio::error::operation_aborted)
          ProcessError(error);
        return;
      }

      connected_ = true;
      delegate()->OnTransportOpened();
    });
  });

  return net::OK;
}

void AsioTcpTransport::Close() {
  connected_ = false;
  resolver_.cancel();
  boost::system::error_code ec;
  socket_.cancel(ec);
}

int AsioTcpTransport::Read(void* data, size_t len) {
  size_t count = std::min(len, read_buffer_.size());
  std::copy(read_buffer_.begin(), read_buffer_.end(), reinterpret_cast<char*>(data));
  read_buffer_.clear();
  StartReading();
  return count;
}

int AsioTcpTransport::Write(const void* data, size_t len) {
  boost::system::error_code ec;
  auto bytes_written = socket_.write_some(boost::asio::buffer(data, len), ec);
  return bytes_written ? bytes_written : net::MapSystemError(ec.value());
}

std::string AsioTcpTransport::GetName() const {
  return "TCP";
}

bool AsioTcpTransport::IsMessageOriented() const {
  return false;
}

bool AsioTcpTransport::IsConnected() const {
  return connected_;
}

void AsioTcpTransport::StartReading() {
  if (reading_)
    return;

  reading_buffer_.resize(read_buffer_.capacity() - read_buffer_.size());
  if (reading_buffer_.empty())
    return;

  reading_ = true;
  socket_.async_receive(boost::asio::buffer(reading_buffer_),
      [this](const boost::system::error_code& ec, std::size_t bytes_transferred) {
        assert(reading_);
        reading_ = false;

        if (ec) {
          if (ec != boost::asio::error::operation_aborted)
            ProcessError(ec);
          return;
        }

        read_buffer_.insert(read_buffer_.end(), reading_buffer_.begin(), reading_buffer_.begin() + bytes_transferred);
        delegate()->OnTransportDataReceived();

        StartReading();
      });
}

void AsioTcpTransport::ProcessError(const boost::system::error_code& ec) {
  assert(connected_);
  connected_ = false;
  if (delegate())
    delegate()->OnTransportClosed(net::MapSystemError(ec.value()));
}

} // namespace net
