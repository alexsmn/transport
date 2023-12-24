#pragma once

#include "net/transport.h"

namespace net {

inline promise<void> OpenWithPromise(Connector& connector,
                                     const Connector::Handlers& handlers) {
  struct State {
    bool done = false;
    promise<void> p;
  };

  auto state = std::make_shared<State>();

  auto promise_handlers = handlers;

  promise_handlers.on_open = [state,
                              original_on_open = handlers.on_open]() mutable {
    original_on_open();

    if (!state->done) {
      state->done = true;
      state->p.resolve();
    }
  };

  promise_handlers.on_close =
      [state, original_on_close = handlers.on_close](Error error) mutable {
        original_on_close(error);

        if (!state->done) {
          state->done = true;
          state->p.reject(error);
        }
      };

  connector.Open(promise_handlers);

  return state->p;
}

}  // namespace net