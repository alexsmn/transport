#pragma once

#include "net/transport.h"

#include <memory>

namespace net {

class acceptor {
 public:
  explicit acceptor(std::unique_ptr<Transport> transport)
      : transport_{std::move(transport)} {}

  acceptor(acceptor&&) = default;
  acceptor& operator=(acceptor&&) = default;

  [[nodiscard]] Executor get_executor() const {
    return transport_->GetExecutor();
  }

  [[nodiscard]] awaitable<Error> open(Transport::Handlers handlers) {
    return transport_->Open(std::move(handlers));
  }

 private:
  std::unique_ptr<Transport> transport_;
};

}  // namespace net