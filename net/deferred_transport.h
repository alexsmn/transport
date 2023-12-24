#pragma once

#include "net/transport.h"

namespace net {

// Provided `set_connected()` method that can be used to rebind the underlying
// transport to another handlers.
class DeferredTransport final : public Transport {
 public:
  explicit DeferredTransport(std::unique_ptr<Transport> underlying_transport);

  // Used for active transports only.
  void set_connected(bool connected) { connected_ = connected; }

  void set_additional_close_handler(CloseHandler handler) {
    additional_close_handler_ = std::move(handler);
  }

  // Transport
  virtual void Open(const Handlers& handlers) override;
  virtual void Close() override;
  virtual int Read(std::span<char> data) override;
  virtual promise<size_t> Write(std::span<const char> data) override;
  virtual std::string GetName() const override;
  virtual bool IsMessageOriented() const override;
  virtual bool IsConnected() const override { return connected_; }
  virtual bool IsActive() const override;

 private:
  Handlers handlers_;
  CloseHandler additional_close_handler_;
  bool connected_ = false;
  std::unique_ptr<Transport> underlying_transport_;
};

}  // namespace net
