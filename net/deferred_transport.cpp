#include "net/deferred_transport.h"

#include "net/transport_util.h"

#include <boost/asio/bind_executor.hpp>
#include <boost/asio/dispatch.hpp>

namespace net {

DeferredTransport::DeferredTransport(
    boost::asio::executor executor,
    std::unique_ptr<Transport> underlying_transport)
    : core_{std::make_shared<Core>(std::move(executor),
                                   std::move(underlying_transport))} {
  assert(core_->underlying_transport_);
}

promise<void> DeferredTransport::Open(const Handlers& handlers) {
  auto [p, promise_handlers] = MakePromiseHandlers(handlers);

  boost::asio::dispatch(
      core_->executor_,
      std::bind_front(&Core::Open, core_, std::move(promise_handlers)));

  return p;
}

void DeferredTransport::Core::Open(const Handlers& handlers) {
  assert(!connected_);

  handlers_ = handlers;
  connected_ = true;

  if (underlying_transport_->IsConnected()) {
    return;
  }

  underlying_transport_->Open(
      {.on_open = boost::asio::bind_executor(
           executor_, std::bind_front(&Core::OnOpened, shared_from_this())),
       .on_close = boost::asio::bind_executor(
           executor_, std::bind_front(&Core::OnClosed, shared_from_this())),
       .on_data = boost::asio::bind_executor(
           executor_, std::bind_front(&Core::OnData, shared_from_this())),
       .on_message =
           [this](std::span<const char> data) {
             // Capture the `data` as a vector.
             boost::asio::dispatch(
                 std::bind_front(&Core::OnMessage, shared_from_this(),
                                 std::vector<char>{data.begin(), data.end()}));
           },
       .on_accept = boost::asio::bind_executor(
           executor_, std::bind_front(&Core::OnAccepted, shared_from_this()))});
}

void DeferredTransport::Core::OnOpened() {
  if (connected_ && handlers_.on_open) {
    handlers_.on_open();
  }
}

void DeferredTransport::Core::OnClosed(Error error) {
  if (!connected_) {
    return;
  }

  // Capture the additional close handler, as the object may be
  // deleted from the normal close handler.
  auto additional_close_handler = additional_close_handler_;

  if (handlers_.on_close) {
    handlers_.on_close(error);
  }

  if (additional_close_handler) {
    additional_close_handler(error);
  }
}

void DeferredTransport::Core::OnData() {
  if (connected_ && handlers_.on_data) {
    handlers_.on_data();
  }
}

void DeferredTransport::Core::OnMessage(std::span<const char> data) {
  if (connected_ && handlers_.on_message) {
    handlers_.on_message(data);
  }
}

void DeferredTransport::Core::OnAccepted(std::unique_ptr<Transport> transport) {
  if (connected_ && handlers_.on_accept) {
    handlers_.on_accept(std::move(transport));
  }
}

void DeferredTransport::Close() {
  boost::asio::dispatch(core_->executor_, std::bind_front(&Core::Close, core_));
}

void DeferredTransport::Core::Close() {
  // So far `Close` can be called repeatedly.

  auto additional_close_handler =
      std::exchange(additional_close_handler_, nullptr);

  handlers_ = {};
  connected_ = false;

  if (underlying_transport_) {
    underlying_transport_->Close();
  }

  if (additional_close_handler) {
    additional_close_handler(OK);
  }
}

int DeferredTransport::Read(std::span<char> data) {
  return core_->connected_ ? core_->underlying_transport_->Read(data)
                           : ERR_ACCESS_DENIED;
}

promise<size_t> DeferredTransport::Write(std::span<const char> data) {
  return core_->connected_ ? core_->underlying_transport_->Write(data)
                           : make_error_promise<size_t>(ERR_ACCESS_DENIED);
}

std::string DeferredTransport::GetName() const {
  return core_->underlying_transport_->GetName();
}

bool DeferredTransport::IsMessageOriented() const {
  return core_->underlying_transport_->IsMessageOriented();
}

bool DeferredTransport::IsActive() const {
  return core_->underlying_transport_->IsActive();
}

}  // namespace net
