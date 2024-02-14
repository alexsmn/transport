#include "net/deferred_transport.h"

#include "base/threading/thread_collision_warner.h"
#include "net/base/bind_util.h"
#include "net/logger.h"
#include "net/transport_util.h"

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

  promise<void> Open(const Handlers& handlers);
  void Close();

  void OnOpened();
  void OnClosed(Error error);
  void OnData();
  void OnMessage(std::span<const char> data);
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

promise<void> DeferredTransport::Open(const Handlers& handlers) {
  return DispatchAsPromise(core_->executor_,
                         std::bind_front(&Core::Open, core_, handlers));
}

promise<void> DeferredTransport::Core::Open(const Handlers& handlers) {
  DFAKE_SCOPED_RECURSIVE_LOCK(mutex_);

  assert(!opened_);

  opened_ = true;

  if (underlying_transport_->IsConnected()) {
    handlers_ = handlers;
    return make_resolved_promise();
  }

  auto [p, promise_handlers] = MakePromiseHandlers(handlers);
  handlers_ = std::move(promise_handlers);

  underlying_transport_->Open(
      {.on_open = boost::asio::bind_executor(
           executor_, BindFrontWeakPtr(&Core::OnOpened, weak_from_this())),
       .on_close = boost::asio::bind_executor(
           executor_, BindFrontWeakPtr(&Core::OnClosed, weak_from_this())),
       .on_data = boost::asio::bind_executor(
           executor_, BindFrontWeakPtr(&Core::OnData, weak_from_this())),
       .on_message =
           [this](std::span<const char> data) {
             // Capture the `data` as a vector.
             boost::asio::dispatch(std::bind_front(
                 BindFrontWeakPtr(&Core::OnMessage, weak_from_this()),
                 std::vector<char>{data.begin(), data.end()}));
           },
       .on_accept = boost::asio::bind_executor(
           executor_, BindFrontWeakPtr(&Core::OnAccepted, weak_from_this()))});

  return p;
}

void DeferredTransport::Core::OnOpened() {
  DFAKE_SCOPED_RECURSIVE_LOCK(mutex_);

  if (opened_ && handlers_.on_open) {
    handlers_.on_open();
  }
}

void DeferredTransport::Core::OnClosed(Error error) {
  DFAKE_SCOPED_RECURSIVE_LOCK(mutex_);

  if (!opened_) {
    return;
  }

  // Capture the additional close handler, as the object may be
  // deleted from the normal close handler.
  auto additional_close_handler = additional_close_handler_;

  if (handlers_.on_close) {
    handlers_.on_close(error);
  }

  if (additional_close_handler) {
    additional_close_handler(error);
  }
}

void DeferredTransport::Core::OnData() {
  DFAKE_SCOPED_RECURSIVE_LOCK(mutex_);

  if (opened_ && handlers_.on_data) {
    handlers_.on_data();
  }
}

void DeferredTransport::Core::OnMessage(std::span<const char> data) {
  DFAKE_SCOPED_RECURSIVE_LOCK(mutex_);

  if (opened_ && handlers_.on_message) {
    handlers_.on_message(data);
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

int DeferredTransport::Read(std::span<char> data) {
  return core_->opened_ ? core_->underlying_transport_->Read(data)
                        : ERR_ACCESS_DENIED;
}

promise<size_t> DeferredTransport::Write(std::span<const char> data) {
  return core_->opened_ ? core_->underlying_transport_->Write(data)
                        : make_error_promise<size_t>(ERR_ACCESS_DENIED);
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

}  // namespace net
