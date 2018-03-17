#include "net/socket_transport.h"

#include "base/strings/stringprintf.h"
#include "net/base/net_errors.h"
#include "net/socket.h"

namespace net {

SocketTransport::SocketTransport() {}

SocketTransport::SocketTransport(std::unique_ptr<Socket> socket)
    : socket_(std::move(socket)) {
  assert(socket_->is_connected());
  connected_ = true;
  socket_->set_delegate(this);
}

SocketTransport::~SocketTransport() {}

void SocketTransport::OnSocketConnected(Error error) {
  assert(active_);
  assert(!connected_);

  if (error == OK) {
    connected_ = true;
    delegate_->OnTransportOpened();

  } else {
    Close();

    if (delegate_)
      delegate_->OnTransportClosed(error);
  }
}

void SocketTransport::OnSocketAccepted(std::unique_ptr<Socket> socket) {
  assert(socket_);
  auto transport = std::make_unique<SocketTransport>(std::move(socket));
  delegate_->OnTransportAccepted(std::move(transport));
}

void SocketTransport::OnSocketClosed(Error error) {
  Close();

  delegate_->OnTransportClosed(error);
}

int SocketTransport::Read(void* data, size_t len) {
  assert(connected_);
  return socket_->Read(data, len);
}

void SocketTransport::OnSocketDataReceived() {
  assert(connected_);
  delegate_->OnTransportDataReceived();
}

int SocketTransport::Write(const void* data, size_t len) {
  if (!socket_)
    return ERR_INVALID_HANDLE;
  assert(connected_);

  if (send_buffer_.capacity() < len)
    send_buffer_.set_capacity(std::max(send_buffer_.capacity() * 2, len));

  send_buffer_.insert(send_buffer_.end(), static_cast<const char*>(data),
                      static_cast<const char*>(data) + len);

  SendNext();

  return len;
}

Error SocketTransport::Open(Transport::Delegate& delegate) {
  assert(!connected_);
  assert(!socket_);
  assert(!sending_);
  assert(send_buffer_.empty());

  delegate_ = &delegate;

  socket_.reset(new Socket(FROM_HERE, this));
  socket_->set_logger(logger);

  Error error = OK;
  if (active_)
    error = socket_->Connect(host_.c_str(), port_);
  else
    error = socket_->Listen(port_);

  if (error != OK) {
    socket_.reset();
    return error;
  }

  if (!active_) {
    connected_ = true;
    delegate_->OnTransportOpened();
  }

  return error;
}

void SocketTransport::Close() {
  socket_.reset();
  connected_ = false;
  send_buffer_.clear();
  sending_ = false;
}

std::string SocketTransport::GetName() const {
  if (active_)
    return base::StringPrintf("Active TCP %s:%d", host_.c_str(),
                              static_cast<int>(port_));

  unsigned ip;
  unsigned short port;
  if (!socket_ || !socket_->GetPeerAddress(ip, port))
    return "TCP";

  unsigned char* ipn = reinterpret_cast<unsigned char*>(&ip);
  return base::StringPrintf("TCP %d.%d.%d.%d:%d", static_cast<int>(ipn[0]),
                            static_cast<int>(ipn[1]), static_cast<int>(ipn[2]),
                            static_cast<int>(ipn[3]), static_cast<int>(port));
}

void SocketTransport::OnSocketSendPossible() {
  assert(connected_);

  sending_ = false;
  SendNext();
}

void SocketTransport::SendNext() {
  if (sending_)
    return;

  if (send_buffer_.empty())
    return;

  sending_ = true;

  while (!send_buffer_.empty()) {
    auto [data, len] = send_buffer_.array_one();
    assert(len != 0);

    int res = socket_->Write(data, len);
    if (res <= 0) {
      if (res != net::ERR_IO_PENDING) {
        // Can't close directly.
        socket_->Shutdown();
      }
      return;
    }

    send_buffer_.erase_begin(res);
  }

  sending_ = false;
}

}  // namespace net
