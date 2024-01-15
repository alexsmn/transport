#pragma once

#include "net/transport.h"

#include <atomic>
#include <boost/asio/executor.hpp>

namespace net {

// Provided `set_connected()` method that can be used to rebind the underlying
// transport to another handlers.
// Thread-safe.
class DeferredTransport final : public Transport {
 public:
  DeferredTransport(boost::asio::executor executor,
                    std::unique_ptr<Transport> underlying_transport);

  // Used for active transports only.
  void set_connected(bool connected) { core_->connected_ = connected; }

  void set_additional_close_handler(CloseHandler handler) {
    core_->additional_close_handler_ = std::move(handler);
  }

  // Transport
  virtual promise<void> Open(const Handlers& handlers) override;
  virtual void Close() override;
  virtual int Read(std::span<char> data) override;
  virtual promise<size_t> Write(std::span<const char> data) override;
  virtual std::string GetName() const override;
  virtual bool IsMessageOriented() const override;
  virtual bool IsConnected() const override { return core_->connected_; }
  virtual bool IsActive() const override;

 private:
  // TODO: Move to the source file.
  struct Core : std::enable_shared_from_this<Core> {
    Core(boost::asio::executor executor,
         std::unique_ptr<Transport> underlying_transport)
        : executor_{std::move(executor)},
          underlying_transport_{std::move(underlying_transport)} {}

    void Open(const Handlers& handlers);
    void Close();

    void OnOpened();
    void OnClosed(Error error);
    void OnData();
    void OnMessage(std::span<const char> data);
    void OnAccepted(std::unique_ptr<Transport> transport);

    boost::asio::executor executor_;
    std::unique_ptr<Transport> underlying_transport_;
    Handlers handlers_;
    CloseHandler additional_close_handler_;
    std::atomic<bool> connected_ = false;
  };

  std::shared_ptr<Core> core_;
};

}  // namespace net
