#pragma once

#include "transport/transport.h"

#include <Windows.h>

namespace transport {

// TODO: Rework with `boost/process/async_pipe.hpp`.
class PipeTransport final : public Transport {
 public:
  explicit PipeTransport(const executor& executor);
  virtual ~PipeTransport();

  void Init(const std::wstring& name, bool server);

  // Transport overrides.
  [[nodiscard]] virtual awaitable<error_code> open() override;
  [[nodiscard]] virtual awaitable<error_code> close() override;
  [[nodiscard]] virtual awaitable<expected<any_transport>> accept() override;

  [[nodiscard]] virtual awaitable<expected<size_t>> read(
      std::span<char> data) override;

  [[nodiscard]] virtual awaitable<expected<size_t>> write(
      std::span<const char> data) override;

  virtual std::string name() const override;
  virtual bool message_oriented() const override { return false; }
  virtual bool connected() const override { return connected_; }
  virtual bool active() const override { return true; }
  virtual executor get_executor() override { return executor_; }

 private:
  executor executor_;

  std::wstring name_;
  bool server_ = false;
  HANDLE handle_ = INVALID_HANDLE_VALUE;

  bool connected_ = false;
};

}  // namespace transport
