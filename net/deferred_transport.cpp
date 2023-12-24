#include "net/deferred_transport.h"

namespace net {

DeferredTransport::DeferredTransport(
    std::unique_ptr<Transport> underlying_transport)
    : underlying_transport_{std::move(underlying_transport)} {}

void DeferredTransport::Open(const Handlers& handlers) {
  assert(!connected_);

  handlers_ = handlers;
  connected_ = true;

  if (underlying_transport_->IsConnected()) {
    return;
  }

  underlying_transport_->Open(
      {.on_open =
           [this] {
             if (connected_ && handlers_.on_open)
               handlers_.on_open();
           },
       .on_close =
           [this](Error error) {
             if (connected_) {
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
           },
       .on_data =
           [this] {
             if (connected_ && handlers_.on_data)
               handlers_.on_data();
           },
       .on_message =
           [this](std::span<const char> data) {
             if (connected_ && handlers_.on_message)
               handlers_.on_message(data);
           },
       .on_accept =
           [this](std::unique_ptr<Transport> transport) {
             if (connected_ && handlers_.on_accept) {
               handlers_.on_accept(std::move(transport));
             }
           }});
}

void DeferredTransport::Close() {
  auto additional_close_handler = additional_close_handler_;

  handlers_ = {};
  connected_ = false;
  underlying_transport_->Close();
  underlying_transport_ = nullptr;

  if (additional_close_handler) {
    additional_close_handler(OK);
  }
}

int DeferredTransport::Read(std::span<char> data) {
  return connected_ ? underlying_transport_->Read(data) : ERR_ACCESS_DENIED;
}

promise<size_t> DeferredTransport::Write(std::span<const char> data) {
  return connected_ ? underlying_transport_->Write(data)
                    : make_error_promise<size_t>(ERR_ACCESS_DENIED);
}

std::string DeferredTransport::GetName() const {
  return underlying_transport_->GetName();
}

bool DeferredTransport::IsMessageOriented() const {
  return underlying_transport_->IsMessageOriented();
}

bool DeferredTransport::IsActive() const {
  return underlying_transport_->IsActive();
}

}  // namespace net
