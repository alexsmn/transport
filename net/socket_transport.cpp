#include "net/socket_transport.h"

#include "base/strings/stringprintf.h"
#include "net/base/net_errors.h"
#include "net/socket.h"

namespace net {

SocketTransport::SocketTransport() {
}

SocketTransport::SocketTransport(std::unique_ptr<Socket> socket)
    : socket_(std::move(socket)) {
  assert(socket_->is_connected());
  connected_ = true;
  socket_->set_delegate(this);
}

SocketTransport::~SocketTransport() {
}

void SocketTransport::OnSocketConnected(Error error) {
  assert(active_);
  assert(!connected_);

  if (error == OK) {
    connected_ = true;
    delegate_->OnTransportOpened();
    
  } else {
    connected_ = false;
    socket_.reset();

    if (delegate_)
      delegate_->OnTransportClosed(error);
  }
}

void SocketTransport::OnSocketAccepted(std::unique_ptr<Socket> socket) {
  assert(socket_.get());
  delegate_->OnTransportAccepted(std::make_unique<SocketTransport>(std::move(socket)));
}

void SocketTransport::OnSocketClosed(Error error) {
  connected_ = false;
  socket_.reset();
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
  if (!socket_.get())
    return ERR_INVALID_HANDLE;
  assert(connected_);
  return socket_->Write(data, len);
}

Error SocketTransport::Open() {
  assert(!connected_);
  assert(!socket_.get());

  socket_.reset(new Socket(FROM_HERE, this));

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
}

std::string SocketTransport::GetName() const {
  if (active_)
    return base::StringPrintf("Active TCP %s:%d", host_.c_str(), static_cast<int>(port_));

  unsigned ip;
  unsigned short port;
  if (!socket_.get() || !socket_->GetPeerAddress(ip, port))
    return "TCP";

  unsigned char* ipn = reinterpret_cast<unsigned char*>(&ip);
  return base::StringPrintf("TCP %d.%d.%d.%d:%d", static_cast<int>(ipn[0]),
                                            static_cast<int>(ipn[1]),
                                            static_cast<int>(ipn[2]),
                                            static_cast<int>(ipn[3]),
                                            static_cast<int>(port));
}

} // namespace net
