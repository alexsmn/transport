#include "net/queue_transport.h"

#include "net/base/net_errors.h"

using namespace std::chrono_literals;

namespace net {

QueueTransport::QueueTransport(const Executor& executor)
    : executor_{executor}, timer_{executor} {
  active_ = false;
}

void QueueTransport::SetActive(QueueTransport& peer) {
  peer_ = &peer;
  active_ = true;
}

[[nodiscard]] awaitable<Error> QueueTransport::Open(Handlers handlers) {
  handlers_ = std::move(handlers);

  if (active_) {
    assert(peer_);
    peer_->OnAccept(*this);
    // TODO: Fix ASAP.
    timer_.StartRepeating(10ms, [this] { Exec(); });
  }

  connected_ = true;

  if (auto on_open = std::move(handlers_.on_open)) {
    on_open();
  }

  co_return OK;
}

void QueueTransport::Close() {
  connected_ = false;
  timer_.Stop();
}

int QueueTransport::Read(std::span<char> data) {
  assert(false);
  return net::ERR_FAILED;
}

awaitable<ErrorOr<size_t>> QueueTransport::Write(
    std::span<const char> data) {
  if (data.empty()) {
    co_return ERR_INVALID_ARGUMENT;
  }

  if (!connected_ || !peer_) {
    co_return net::ERR_FAILED;
  }

  peer_->read_queue_.emplace(data.begin(), data.end());
  co_return data.size();
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
  if (handlers_.on_message)
    handlers_.on_message(message);
}

std::string QueueTransport::GetName() const {
  return "Queue";
}

void QueueTransport::OnMessage(const void* data, size_t size) {
  assert(connected_);

  if (handlers_.on_message) {
    handlers_.on_message({static_cast<const char*>(data), size});
  }
}

void QueueTransport::OnAccept(QueueTransport& transport) {
  assert(!peer_);
  assert(connected_);

  if (!handlers_.on_accept)
    return;

  auto t = std::make_unique<QueueTransport>(executor_);
  t->peer_ = &transport;
  t->connected_ = true;
  t->active_ = false;
  // TODO: Fix ASAP.
  t->timer_.StartRepeating(10ms, [this] { Exec(); });

  transport.peer_ = t.get();
  handlers_.on_accept(std::move(t));
}

}  // namespace net
