#include "transport/any_transport.h"

#include "transport/transport.h"

namespace transport {

namespace {

// Self-contained "no transport" results. These coroutines capture nothing, so
// they stay valid however long the caller holds them.

awaitable<error_code> InvalidHandleCode() {
  co_return ERR_INVALID_HANDLE;
}

awaitable<expected<size_t>> InvalidHandleSize() {
  co_return ERR_INVALID_HANDLE;
}

awaitable<expected<any_transport>> InvalidHandleTransport() {
  co_return ERR_INVALID_HANDLE;
}

}  // namespace

any_transport::~any_transport() = default;

any_transport::any_transport(any_transport&&) = default;

any_transport& any_transport::operator=(any_transport&&) = default;

void any_transport::reset() {
  transport_.reset();
}

executor any_transport::get_executor() {
  return transport_ ? transport_->get_executor() : executor{};
}

std::string any_transport::name() const {
  return transport_ ? transport_->name() : std::string{};
}

std::string any_transport::peer() const {
  return transport_ ? transport_->peer() : std::string{};
}

bool any_transport::message_oriented() const {
  return transport_ && transport_->message_oriented();
}

bool any_transport::active() const {
  return transport_ && transport_->active();
}

bool any_transport::connected() const {
  return transport_ && transport_->connected();
}

// The five forwarders below are deliberately NOT coroutines: each reads
// `transport_` while the caller is still inside the call, which is the only
// moment this object is known to be alive, and hands back the underlying
// transport's own awaitable rather than interposing a frame that would outlive
// `this`. Turning any of them back into a coroutine reintroduces the
// use-after-free described in the header.

awaitable<error_code> any_transport::open() {
  return transport_ ? transport_->open() : InvalidHandleCode();
}

awaitable<error_code> any_transport::close() {
  // A moved-from or already-reset transport is a valid state here: teardown
  // paths can race (e.g. a queued close coroutine running after the owner
  // released the transport), so report it instead of asserting.
  return transport_ ? transport_->close() : InvalidHandleCode();
}

awaitable<expected<any_transport>> any_transport::accept() {
  return transport_ ? transport_->accept() : InvalidHandleTransport();
}

awaitable<expected<size_t>> any_transport::read(std::span<char> data) const {
  return transport_ ? transport_->read(data) : InvalidHandleSize();
}

awaitable<expected<size_t>> any_transport::write(
    std::span<const char> data) const {
  return transport_ ? transport_->write(data) : InvalidHandleSize();
}

}  // namespace transport
