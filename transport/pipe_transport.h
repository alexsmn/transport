#pragma once

#include "transport/transport.h"

#include <Windows.h>

namespace transport {

// TODO: Rework with `boost/process/async_pipe.hpp`.
class PipeTransport final : public Transport {
 public:
  explicit PipeTransport(const Executor& executor);
  virtual ~PipeTransport();

  void Init(const std::wstring& name, bool server);

  // Transport overrides.
  [[nodiscard]] virtual awaitable<Error> open() override;
  [[nodiscard]] virtual awaitable<Error> close() override;
  [[nodiscard]] virtual awaitable<ErrorOr<any_transport>> accept() override;

  [[nodiscard]] virtual awaitable<ErrorOr<size_t>> read(
      std::span<char> data) override;

  [[nodiscard]] virtual awaitable<ErrorOr<size_t>> write(
      std::span<const char> data) override;

  virtual std::string name() const override;
  virtual bool message_oriented() const override { return false; }
  virtual bool connected() const override { return connected_; }
  virtual bool active() const override { return true; }
  virtual Executor get_executor() override { return executor_; }

 private:
  Executor executor_;

  std::wstring name_;
  bool server_ = false;
  HANDLE handle_ = INVALID_HANDLE_VALUE;

  bool connected_ = false;
};

}  // namespace transport
