#pragma once

#include "net/opened_transport.h"
#include "net/transport_util.h"

#include <memory>

namespace net {

class connector {
 public:
  explicit connector(std::unique_ptr<Transport> transport)
      : transport_(std::move(transport)) {}

  connector(connector&&) = default;
  connector& operator=(connector&&) = default;

  struct Handlers {
    // TODO: Remove `on_open` and keep only `on_accept`.
    Connector::OpenHandler on_open;
    // Triggered also when open fails.
    Connector::CloseHandler on_close;
    // For streaming transports.
    // TODO: Remove and substitute with a promised `Read`.
    Connector::DataHandler on_data;
    // For message-oriented transports.
    // TODO: Remove and substitute with a promised `Read`.
    Connector::MessageHandler on_message;
    // TODO: Introduce an `Accept` method returning a promised transport.
    Connector::AcceptHandler on_accept;
  };

  promise<opened_transport> open(const Handlers& handlers) {
    if (!transport_) {
      return make_error_promise<opened_transport>(ERR_INVALID_HANDLE);
    }

    if (transport_->IsActive()) {
    }

    return OpenWithPromise(std::move(transport_),
                           MakeConnectorHandlers(handlers));
  }

 private:
  static Connector::Handlers MakeConnectorHandlers(const Handlers& handlers) {
    return {.on_open = handlers.on_open,
            .on_close = handlers.on_close,
            .on_data = handlers.on_data,
            .on_message = handlers.on_message,
            .on_accept = handlers.on_accept};
  }

  std::unique_ptr<Transport> transport_;
};

}  // namespace net