#pragma once

#include "net/base/net_export.h"
#include "net/timer.h"
#include "net/transport.h"

#include <windows.h>

namespace net {

// TODO: Rework with `boost/process/async_pipe.hpp`.
class NET_EXPORT PipeTransport final : public Transport {
 public:
  explicit PipeTransport(boost::asio::io_service& io_service);
  virtual ~PipeTransport();

  void Init(const std::wstring& name, bool server);

  // Transport overrides.
  virtual void Open(const Handlers& handlers) override;
  virtual void Close() override;
  virtual int Read(std::span<char> data) override;
  virtual int Write(std::span<const char> data) override;
  virtual std::string GetName() const override;
  virtual bool IsMessageOriented() const override { return false; }
  virtual bool IsConnected() const override { return connected_; }
  virtual bool IsActive() const override { return true; }

 private:
  void OnTimer();

  Handlers handlers_;

  std::wstring name_;
  bool server_ = false;
  HANDLE handle_ = INVALID_HANDLE_VALUE;

  Timer timer_;
  bool connected_ = false;
};

}  // namespace net
