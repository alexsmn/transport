#pragma once

#include "net/base/net_export.h"
#include "net/timer.h"
#include "net/transport.h"

#include <windows.h>

namespace net {

// TODO: Rework with `boost/process/async_pipe.hpp`.
class NET_EXPORT PipeTransport final : public Transport {
 public:
  explicit PipeTransport(const Executor& executor);
  virtual ~PipeTransport();

  void Init(const std::wstring& name, bool server);

  // Transport overrides.
  [[nodiscard]] virtual awaitable<Error> Open(Handlers handlers) override;

  virtual void Close() override;

  [[nodiscard]] virtual awaitable<ErrorOr<size_t>> Read(
      std::span<char> data) override;

  [[nodiscard]] virtual awaitable<ErrorOr<size_t>> Write(
      std::span<const char> data) override;

  virtual std::string GetName() const override;
  virtual bool IsMessageOriented() const override { return false; }
  virtual bool IsConnected() const override { return connected_; }
  virtual bool IsActive() const override { return true; }
  virtual Executor GetExecutor() const override { return executor_; }

 private:
  void OnTimer();

  Executor executor_;
  Handlers handlers_;

  std::wstring name_;
  bool server_ = false;
  HANDLE handle_ = INVALID_HANDLE_VALUE;

  Timer timer_;
  bool connected_ = false;
};

}  // namespace net
