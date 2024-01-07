#pragma once

#include "net/opened_transport.h"
#include "net/transport.h"

namespace net {

inline promise<opened_transport> OpenWithPromise(
    std::unique_ptr<Transport> transport,
    const Connector::Handlers& handlers) {
  if (!transport) {
    return make_error_promise<opened_transport>(ERR_INVALID_HANDLE);
  }

  struct State {
    std::unique_ptr<Transport> transport;
    bool done = false;
    promise<opened_transport> p;
  };

  auto state = std::make_shared<State>(std::move(transport));

  auto promise_handlers = handlers;

  promise_handlers.on_open = [state, original_on_open = handlers.on_open] {
    original_on_open();

    if (!state->done) {
      state->done = true;
      state->p.resolve(opened_transport{std::move(state->transport)});
    }
  };

  promise_handlers.on_close = [state, original_on_close =
                                          handlers.on_close](Error error) {
    original_on_close(error);

    if (!state->done) {
      state->done = true;
      state->p.reject(error);
    }
  };

  state->transport->Open(promise_handlers);

  return state->p;
}

}  // namespace net