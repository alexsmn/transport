#include "transport/any_transport.h"

#include "transport/transport.h"

namespace transport {

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

bool any_transport::message_oriented() const {
  return transport_ && transport_->message_oriented();
}

bool any_transport::active() const {
  return transport_ && transport_->active();
}

bool any_transport::connected() const {
  return transport_ && transport_->connected();
}

awaitable<error_code> any_transport::open() {
  if (!transport_) {
    co_return ERR_INVALID_HANDLE;
  }

  co_return co_await transport_->open();
}

awaitable<error_code> any_transport::close() {
  assert(transport_);

  if (!transport_) {
    co_return ERR_INVALID_HANDLE;
  }

  co_return co_await transport_->close();
}

awaitable<expected<any_transport>> any_transport::accept() {
  if (!transport_) {
    co_return ERR_INVALID_HANDLE;
  }

  NET_ASSIGN_OR_CO_RETURN(auto transport, co_await transport_->accept());

  co_return any_transport{std::move(transport)};
}

awaitable<expected<size_t>> any_transport::read(std::span<char> data) const {
  if (!transport_) {
    co_return ERR_INVALID_HANDLE;
  }

  co_return co_await transport_->read(data);
}

awaitable<expected<size_t>> any_transport::write(
    std::span<const char> data) const {
  if (!transport_) {
    co_return ERR_INVALID_HANDLE;
  }

  co_return co_await transport_->write(data);
}

}  // namespace transport