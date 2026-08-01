#pragma once

#include "transport/awaitable.h"
#include "transport/detail/wrapped_transport.h"
#include "transport/error.h"
#include "transport/executor.h"

#include <memory>
#include <span>

namespace transport {

class Transport;

class any_transport {
 public:
  any_transport() = default;

  template <std::derived_from<Transport> T>
  explicit any_transport(std::unique_ptr<T> transport)
      : transport_{std::move(transport)} {}

  template <typename T>
  explicit any_transport(T&& transport)
      : transport_{std::make_unique<detail::WrappedTransport<T>>(
            std::forward<T>(transport))} {}

  ~any_transport();

  any_transport(any_transport&&);
  any_transport& operator=(any_transport&&);

  explicit operator bool() const { return transport_ != nullptr; }

  void reset();

  [[nodiscard]] executor get_executor();
  [[nodiscard]] std::string name() const;
  // Remote peer as "address:port"; see TransportMetadata::peer().
  [[nodiscard]] std::string peer() const;
  [[nodiscard]] bool message_oriented() const;
  [[nodiscard]] bool active() const;
  [[nodiscard]] bool connected() const;

  [[nodiscard]] awaitable<error_code> open();
  [[nodiscard]] awaitable<error_code> close();
  [[nodiscard]] awaitable<expected<any_transport>> accept();
  [[nodiscard]] awaitable<expected<size_t>> read(std::span<char> data) const;
  [[nodiscard]] awaitable<expected<size_t>> write(
      std::span<const char> data) const;

 private:
  // Ownership is deliberately unique: destroying the transport is how this
  // codebase cancels operations already in flight. Anything that keeps the
  // transport alive from a pending read instead deadlocks — the read only
  // completes when the transport goes away, and the transport only goes away
  // when the read completes.
  //
  // That is why the async methods above are thin, *non-coroutine* forwarders.
  // A coroutine body runs on first resume, not at call time, so reading
  // `transport_` inside one dereferences `this` long after the call returned.
  // An `any_transport` destroyed in that window made even the `!transport_`
  // guard read freed memory: it saw non-null garbage, passed, and the next
  // dereference faulted — the 2026-08-01 SIGSEGV on the demo's iec104 tier
  // (see any_transport_unittest.cpp). Reading the member while the caller is
  // still inside the call is what makes the guard a guard.
  //
  // The awaitable handed back is the underlying transport's own, so its
  // lifetime rules are that transport's to keep (`AsioTransport::read`/`write`
  // resume with a dangling `this` but touch only locals — a fragile invariant
  // worth preserving when editing them).
  std::unique_ptr<Transport> transport_;
};

}  // namespace transport
