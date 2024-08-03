#pragma once

#include "transport/executor.h"
#include "transport/transport.h"

namespace transport {

// Provided `set_connected()` method that can be used to rebind the underlying
// transport to another handlers.
// Thread-safe.
class DeferredTransport final : public Transport {
 public:
  explicit DeferredTransport(std::unique_ptr<Transport> underlying_transport);
  ~DeferredTransport();

  // Refactor so it's not needed. Or document.
  using CloseHandler = std::function<void(Error error)>;
  void set_additional_close_handler(CloseHandler handler);

  // Transport
  [[nodiscard]] virtual awaitable<Error> Open() override;

  virtual awaitable<Error> Close() override;

  [[nodiscard]] virtual awaitable<ErrorOr<std::unique_ptr<Transport>>> Accept()
      override;

  [[nodiscard]] virtual awaitable<ErrorOr<size_t>> Read(
      std::span<char> data) override;

  [[nodiscard]] virtual awaitable<ErrorOr<size_t>> Write(
      std::span<const char> data) override;

  virtual std::string GetName() const override;
  virtual bool IsMessageOriented() const override;
  virtual bool IsConnected() const override;
  virtual bool IsActive() const override;
  virtual Executor GetExecutor() const override;

 private:
  struct Core;
  std::shared_ptr<Core> core_;
};

}  // namespace transport
