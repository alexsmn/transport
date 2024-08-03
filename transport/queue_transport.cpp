#include "transport/queue_transport.h"

#include "transport/error.h"

using namespace std::chrono_literals;

namespace transport {

QueueTransport::QueueTransport(const Executor& executor)
    : executor_{executor}, timer_{executor} {
  active_ = false;
}

void QueueTransport::SetActive(QueueTransport& peer) {
  peer_ = &peer;
  active_ = true;
}

[[nodiscard]] awaitable<Error> QueueTransport::Open() {
  if (active_) {
    assert(peer_);
    peer_->OnAccept(*this);
    // TODO: Fix ASAP.
    timer_.StartRepeating(10ms, [this] { Exec(); });
  }

  connected_ = true;

  co_return OK;
}

awaitable<Error> QueueTransport::Close() {
  connected_ = false;
  timer_.Stop();
  co_return OK;
}

awaitable<ErrorOr<std::unique_ptr<Transport>>> QueueTransport::Accept() {
  assert(false);
  co_return ERR_FAILED;
}

awaitable<ErrorOr<size_t>> QueueTransport::Read(std::span<char> data) {
  assert(false);
  co_return ERR_FAILED;
}

awaitable<ErrorOr<size_t>> QueueTransport::Write(std::span<const char> data) {
  if (data.empty()) {
    co_return ERR_INVALID_ARGUMENT;
  }

  if (!connected_ || !peer_) {
    co_return ERR_FAILED;
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
}

std::string QueueTransport::GetName() const {
  return "Queue";
}

void QueueTransport::OnMessage(const void* data, size_t size) {
  assert(connected_);
}

void QueueTransport::OnAccept(QueueTransport& transport) {
  assert(!peer_);
  assert(connected_);

  auto t = std::make_unique<QueueTransport>(executor_);
  t->peer_ = &transport;
  t->connected_ = true;
  t->active_ = false;
  // TODO: Fix ASAP.
  t->timer_.StartRepeating(10ms, [this] { Exec(); });

  transport.peer_ = t.get();
  // handlers_.on_accept(std::move(t));
}

}  // namespace transport
