#pragma once

#include "transport/awaitable.h"
#include "transport/error.h"
#include "transport/executor.h"

#include <memory>
#include <span>

namespace transport {

class Transport;

class any_transport {
 public:
  any_transport() = default;
  explicit any_transport(std::unique_ptr<Transport> transport);
  ~any_transport();

  any_transport(any_transport&&);
  any_transport& operator=(any_transport&&);

  explicit operator bool() const { return transport_ != nullptr; }

  [[nodiscard]] Executor get_executor();
  [[nodiscard]] std::string name() const;
  [[nodiscard]] bool message_oriented() const;
  [[nodiscard]] bool active() const;
  [[nodiscard]] bool connected() const;

  [[nodiscard]] awaitable<Error> open();
  [[nodiscard]] awaitable<Error> close();
  [[nodiscard]] awaitable<ErrorOr<any_transport>> accept();
  [[nodiscard]] awaitable<ErrorOr<size_t>> read(std::span<char> data) const;
  [[nodiscard]] awaitable<ErrorOr<size_t>> write(
      std::span<const char> data) const;

 private:
  std::unique_ptr<Transport> transport_;
};

}  // namespace transport
