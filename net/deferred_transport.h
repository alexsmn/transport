#pragma once

#include "net/executor.h"
#include "net/transport.h"

namespace net {

// Provided `set_connected()` method that can be used to rebind the underlying
// transport to another handlers.
// Thread-safe.
class DeferredTransport final : public Transport {
 public:
  DeferredTransport(const Executor& executor,
                    std::unique_ptr<Transport> underlying_transport);
  ~DeferredTransport();

  // Used for active transports only.
  void AllowReOpen();

  // Refactor so it's not needed. Or document.
  void set_additional_close_handler(CloseHandler handler);

  // Transport
  [[nodiscard]] virtual awaitable<Error> Open(Handlers handlers) override;

  virtual void Close() override;

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

}  // namespace net
