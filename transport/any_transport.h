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

  void reset();

  [[nodiscard]] executor get_executor();
  [[nodiscard]] std::string name() const;
  [[nodiscard]] bool message_oriented() const;
  [[nodiscard]] bool active() const;
  [[nodiscard]] bool connected() const;

  [[nodiscard]] awaitable<error_code> open();
  [[nodiscard]] awaitable<error_code> close();
  [[nodiscard]] awaitable<expected<any_transport>> accept();
  [[nodiscard]] awaitable<expected<size_t>> read(std::span<char> data) const;
  [[nodiscard]] awaitable<expected<size_t>> write(
      std::span<const char> data) const;

 private:
  std::unique_ptr<Transport> transport_;
};

}  // namespace transport
