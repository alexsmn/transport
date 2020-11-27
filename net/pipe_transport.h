#pragma once

#include "net/base/net_export.h"
#include "net/timer.h"
#include "net/transport.h"

#include <windows.h>

namespace net {

class NET_EXPORT PipeTransport final : public Transport {
 public:
  explicit PipeTransport(boost::asio::io_service& io_service);
  virtual ~PipeTransport();

  void Init(const std::wstring& name, bool server);

  // Transport overrides.
  virtual Error Open(Transport::Delegate& delegate) override;
  virtual void Close() override;
  virtual int Read(void* data, size_t len) override;
  virtual int Write(const void* data, size_t len) override;
  virtual std::string GetName() const override;
  virtual bool IsMessageOriented() const override { return false; }
  virtual bool IsConnected() const override { return connected_; }
  virtual bool IsActive() const override { return true; }

 private:
  void OnTimer();

  Transport::Delegate* delegate_ = nullptr;

  std::wstring name_;
  bool server_ = false;
  HANDLE handle_ = INVALID_HANDLE_VALUE;

  Timer timer_;
  bool connected_ = false;
};

}  // namespace net
