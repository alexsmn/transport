#pragma once

#include "transport/any_transport.h"
#include "transport/executor.h"
#include "transport/transport.h"

namespace transport {

// Provided `set_connected()` method that can be used to rebind the underlying
// transport to another handlers.
// Thread-safe.
class DeferredTransport final : public Transport {
 public:
  explicit DeferredTransport(any_transport underlying_transport);
  ~DeferredTransport();

  // Refactor so it's not needed. Or document.
  using CloseHandler = std::function<void(error_code error)>;
  void set_additional_close_handler(CloseHandler handler);

  // Transport
  virtual awaitable<error_code> open() override;
  virtual awaitable<error_code> close() override;
  virtual awaitable<expected<any_transport>> accept() override;
  virtual awaitable<expected<size_t>> read(std::span<char> data) override;
  virtual awaitable<expected<size_t>> write(
      std::span<const char> data) override;
  virtual std::string name() const override;
  virtual bool message_oriented() const override;
  virtual bool connected() const override;
  virtual bool active() const override;
  virtual executor get_executor() override;

 private:
  struct Core;
  std::shared_ptr<Core> core_;
};

}  // namespace transport
