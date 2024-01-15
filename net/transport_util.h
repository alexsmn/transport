#pragma once

#include "net/transport.h"

#include <boost/asio/dispatch.hpp>
#include <boost/asio/executor.hpp>

namespace net {

inline std::pair<promise<void>, Connector::Handlers> MakePromiseHandlers(
    const Connector::Handlers& handlers) {
  struct State {
    bool done = false;
    promise<void> p;
  };

  auto state = std::make_shared<State>();

  auto promise_handlers = handlers;

  promise_handlers.on_open = [state, original_on_open = handlers.on_open] {
    original_on_open();

    if (!state->done) {
      state->done = true;
      state->p.resolve();
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

  return {state->p, std::move(promise_handlers)};
}

template <class F>
promise<void> DispatchPromise(const boost::asio::executor& ex, F&& f) {
  promise<void> p;
  boost::asio::dispatch(ex, [p, f = std::forward<F>(f)]() mutable {
    f().then([p]() mutable { p.resolve(); },
             [p](std::exception_ptr e) mutable { p.reject(e); });
  });
  return p;
}

}  // namespace net