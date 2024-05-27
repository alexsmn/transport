#include "net/deferred_transport.h"

#include "net/base/threading/thread_collision_warner.h"
#include "net/base/bind_util.h"
#include "net/logger.h"

#include <boost/asio/bind_executor.hpp>
#include <boost/asio/dispatch.hpp>

namespace net {

// DeferredTransport::Core

// All calls must be dispatched over the executor, including the destructor.
// The core is destroyed when the deferred transport is destroyed. It only uses
// weak pointers to itself internally.
struct DeferredTransport::Core : std::enable_shared_from_this<Core> {
  Core(const Executor& executor,
       std::unique_ptr<Transport> underlying_transport)
      : executor_{executor},
        underlying_transport_{std::move(underlying_transport)} {
    assert(underlying_transport_);
  }

  ~Core();

  [[nodiscard]] awaitable<Error> Open(Handlers handlers);
  void Close();

  void OnClosed(Error error);
  void OnAccepted(std::unique_ptr<Transport> transport);

  DFAKE_MUTEX(mutex_);

  Executor executor_;
  std::unique_ptr<Transport> underlying_transport_;
  Handlers handlers_;
  CloseHandler additional_close_handler_;
  std::atomic<bool> opened_ = false;
};

// DeferredTransport

DeferredTransport::DeferredTransport(
    const Executor& executor,
    std::unique_ptr<Transport> underlying_transport)
    : core_{std::make_shared<Core>(executor, std::move(underlying_transport))} {
}

DeferredTransport ::~DeferredTransport() {
  // Destroy the core under the executor.
  auto executor = core_->executor_;
  boost::asio::dispatch(std::move(executor), [core = std::move(core_)] {});
  assert(!core_);
}

DeferredTransport::Core::~Core() {
  DFAKE_SCOPED_RECURSIVE_LOCK(mutex_);
}

void DeferredTransport::AllowReOpen() {
  core_->opened_ = false;
}

void DeferredTransport::set_additional_close_handler(CloseHandler handler) {
  boost::asio::dispatch(core_->executor_,
                        [core = core_, handler = std::move(handler)]() mutable {
  // A workaround for `DFAKE_SCOPED_RECURSIVE_LOCK` not supporting expressions.
#if !defined(NDEBUG)
                          auto& mutex = core->mutex_;
                          DFAKE_SCOPED_RECURSIVE_LOCK(mutex);
#endif
                          core->additional_close_handler_ = std::move(handler);
                        });
}

bool DeferredTransport::IsConnected() const {
  return core_->opened_;
}

awaitable<Error> DeferredTransport::Open(Handlers handlers) {
  return core_->Open(std::move(handlers));
}

awaitable<Error> DeferredTransport::Core::Open(Handlers handlers) {
  DFAKE_SCOPED_RECURSIVE_LOCK(mutex_);

  assert(!opened_);

  opened_ = true;
  handlers_ = std::move(handlers);

  if (underlying_transport_->IsConnected()) {
    co_return OK;
  }

  auto weak_ref = weak_from_this();

  auto open_result = co_await underlying_transport_->Open(
      {.on_accept = boost::asio::bind_executor(
           executor_, BindFrontWeakPtr(&Core::OnAccepted, weak_from_this()))});

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
  DFAKE_SCOPED_RECURSIVE_LOCK(mutex_);

  if (!opened_) {
    return;
  }

  // WARNING: The object may be deleted from the handler.
  if (additional_close_handler_) {
    additional_close_handler_(error);
  }
}

void DeferredTransport::Core::OnAccepted(std::unique_ptr<Transport> transport) {
  DFAKE_SCOPED_RECURSIVE_LOCK(mutex_);

  if (opened_ && handlers_.on_accept) {
    handlers_.on_accept(std::move(transport));
  }
}

void DeferredTransport::Close() {
  boost::asio::dispatch(core_->executor_, std::bind_front(&Core::Close, core_));
}

void DeferredTransport::Core::Close() {
  auto additional_close_handler =
      std::exchange(additional_close_handler_, nullptr);

  handlers_ = {};
  opened_ = false;

  underlying_transport_->Close();

  if (additional_close_handler) {
    additional_close_handler(OK);
  }
}

awaitable<ErrorOr<size_t>> DeferredTransport::Read(std::span<char> data) {
  if (!core_->opened_) {
    co_return ERR_ACCESS_DENIED;
  }

  co_return co_await core_->underlying_transport_->Read(data);
}

awaitable<ErrorOr<size_t>> DeferredTransport::Write(
    std::span<const char> data) {
  if (!core_->opened_) {
    co_return ERR_ACCESS_DENIED;
  }

  co_return co_await core_->underlying_transport_->Write(data);
}

std::string DeferredTransport::GetName() const {
  return core_->underlying_transport_->GetName();
}

bool DeferredTransport::IsMessageOriented() const {
  return core_->underlying_transport_->IsMessageOriented();
}

bool DeferredTransport::IsActive() const {
  return core_->underlying_transport_->IsActive();
}

Executor DeferredTransport::GetExecutor() const {
  return core_->underlying_transport_->GetExecutor();
}

}  // namespace net
