#include "transport/deferred_transport.h"

#include "transport/bind_util.h"
#include "transport/logger.h"

#include <boost/asio/bind_executor.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/use_awaitable.hpp>

namespace transport {

// DeferredTransport::Core

// All calls must be dispatched over the executor, including the destructor.
// The core is destroyed when the deferred transport is destroyed. It only uses
// weak pointers to itself internally.
struct DeferredTransport::Core : std::enable_shared_from_this<Core> {
  explicit Core(any_transport underlying_transport)
      : executor_{underlying_transport.get_executor()},
        underlying_transport_{std::move(underlying_transport)} {
    assert(underlying_transport_);
  }

  ~Core();

  [[nodiscard]] awaitable<Error> Open();
  [[nodiscard]] awaitable<Error> Close();

  void OnClosed(Error error);

  Executor executor_;
  any_transport underlying_transport_;
  CloseHandler additional_close_handler_;
};

// DeferredTransport

DeferredTransport::DeferredTransport(any_transport underlying_transport)
    : core_{std::make_shared<Core>(std::move(underlying_transport))} {}

DeferredTransport ::~DeferredTransport() {
  // Destroy the core under the executor.
  auto executor = core_->executor_;
  boost::asio::dispatch(std::move(executor), [core = std::move(core_)] {});
  assert(!core_);
}

DeferredTransport::Core::~Core() {}

void DeferredTransport::set_additional_close_handler(CloseHandler handler) {
  boost::asio::dispatch(core_->executor_,
                        [core = core_, handler = std::move(handler)]() mutable {
                          core->additional_close_handler_ = std::move(handler);
                        });
}

bool DeferredTransport::connected() const {
  return core_->underlying_transport_.connected();
}

awaitable<Error> DeferredTransport::open() {
  return core_->Open();
}

awaitable<Error> DeferredTransport::Core::Open() {
  auto weak_ref = weak_from_this();
  auto open_result = co_await underlying_transport_.open();

  if (weak_ref.expired()) {
    co_return ERR_ABORTED;
  }

  if (open_result != OK) {
    OnClosed(open_result);
    co_return open_result;
  }

  co_return OK;
}

void DeferredTransport::Core::OnClosed(Error error) {
  // WARNING: The object may be deleted from the handler.
  if (additional_close_handler_) {
    additional_close_handler_(error);
  }
}

awaitable<Error> DeferredTransport::close() {
  co_return co_await core_->Close();
}

awaitable<Error> DeferredTransport::Core::Close() {
  co_await boost::asio::post(executor_, boost::asio::use_awaitable);

  auto additional_close_handler =
      std::exchange(additional_close_handler_, nullptr);

  auto result = co_await underlying_transport_.close();

  if (additional_close_handler) {
    additional_close_handler(result);
  }

  co_return result;
}

awaitable<ErrorOr<any_transport>> DeferredTransport::accept() {
  co_return co_await core_->underlying_transport_.accept();
}

awaitable<ErrorOr<size_t>> DeferredTransport::read(std::span<char> data) {
  NET_ASSIGN_OR_CO_RETURN(auto bytes_transferred,
                          co_await core_->underlying_transport_.read(data));

  if (bytes_transferred == 0) {
    core_->OnClosed(OK);
  }

  co_return bytes_transferred;
}

awaitable<ErrorOr<size_t>> DeferredTransport::write(
    std::span<const char> data) {
  co_return co_await core_->underlying_transport_.write(data);
}

std::string DeferredTransport::name() const {
  return core_->underlying_transport_.name();
}

bool DeferredTransport::message_oriented() const {
  return core_->underlying_transport_.message_oriented();
}

bool DeferredTransport::active() const {
  return core_->underlying_transport_.active();
}

Executor DeferredTransport::get_executor() const {
  return core_->underlying_transport_.get_executor();
}

}  // namespace transport
