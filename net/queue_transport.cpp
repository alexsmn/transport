#include "net/queue_transport.h"

#include "net/base/net_errors.h"

using namespace std::chrono_literals;

namespace net {

QueueTransport::QueueTransport(boost::asio::io_service& io_service)
    : io_service_{io_service},
      timer_{io_service} {
  active_ = false;
}

void QueueTransport::SetActive(QueueTransport& peer) {
  peer_ = &peer;
  active_ = true;
}

Error QueueTransport::Open() {
  if (active_) {
    assert(peer_);
    peer_->OnAccept(*this);
    // TODO: Fix ASAP.
    timer_.StartRepeating(10ms, [this] { Exec(); });
  }

  connected_ = true;

  if (delegate_)
    delegate_->OnTransportOpened();

  return OK;
}

void QueueTransport::Close() {
  connected_ = false;
  timer_.Stop();
}

int QueueTransport::Read(void* data, size_t len) {
  assert(false);
  return net::ERR_FAILED;
}

int QueueTransport::Write(const void* data, size_t len) {
  assert(len > 0);
  if (!connected_ || !peer_)
    return net::ERR_FAILED;
  const char* chars = static_cast<const char*>(data);
  peer_->read_queue_.push(Message(chars, chars + len));
  return static_cast<int>(len);
}

void QueueTransport::Exec() {
  assert(connected_);
  assert(peer_);

  if (read_queue_.empty())
    return;

  Message message;
  message.swap(read_queue_.front());
  read_queue_.pop();

  assert(!message.empty());
  if (delegate_)
    delegate_->OnTransportMessageReceived(&message[0], message.size());
}

std::string QueueTransport::GetName() const {
  return "Queue";
}

void QueueTransport::OnMessage(const void* data, size_t size) {
  assert(connected_);

  if (delegate_)
    delegate_->OnTransportMessageReceived(data, size);    
}

void QueueTransport::OnAccept(QueueTransport& transport) {
  assert(!peer_);
  assert(connected_);
  
  if (!delegate_)
    return;

  auto t = std::make_unique<QueueTransport>(io_service_);
  t->peer_ = &transport;
  t->connected_ = true;
  t->active_ = false;
  // TODO: Fix ASAP.
  t->timer_.StartRepeating(10ms, [this] { Exec(); });

  auto* tt = t.get();
  if (delegate_->OnTransportAccepted(std::move(t)) == net::OK)
    transport.peer_ = tt;
}

} // namespace net
