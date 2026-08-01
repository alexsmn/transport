#pragma once

#include "transport/awaitable.h"
#include "transport/expected.h"

#include <boost/asio/experimental/channel.hpp>
#include <memory>
#include <span>

namespace transport {

class any_transport;

class WriteQueue {
 public:
  explicit WriteQueue(any_transport& transport);

  void BlindWrite(std::span<const char> data);

  awaitable<expected<size_t>> Write(std::span<const char> data);

 private:
  using Channel =
      boost::asio::experimental::channel<void(boost::system::error_code)>;

  struct State {
    State(any_transport& transport, std::weak_ptr<const void> cancelation)
        : transport{&transport}, cancelation{std::move(cancelation)} {}

    // Borrowed, and only valid while `cancelation` is unexpired: the queue
    // outlives nothing, but its queued coroutines outlive both it and the
    // transport it points at.
    any_transport* transport;
    std::shared_ptr<Channel> last_write;
    std::weak_ptr<const void> cancelation;
  };

  // Taken by value: a queued write suspends, so a `const&` bound to the
  // caller's shared_ptr would dangle once that caller is gone.
  static awaitable<expected<size_t>> Write(std::shared_ptr<State> state,
                                           std::span<const char> data);

  // Owned here, and *only* here, so it expires when the queue does. It used to
  // live in State, which every queued coroutine keeps alive by holding the
  // state — so the `cancelation.expired()` checks below could never fire and a
  // write queued behind another would happily dereference `state->transport`
  // after the transport it points at had been destroyed. Keep this member
  // declared before `state_` so State can capture a weak reference to it.
  std::shared_ptr<const void> cancelation_ = std::make_shared<const char>('\0');

  std::shared_ptr<State> state_;
};

}  // namespace transport
