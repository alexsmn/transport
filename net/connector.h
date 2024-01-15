#pragma once

#include "net/transport.h"

#include <memory>

namespace net {

class connector {
 public:
  explicit connector(std::unique_ptr<Transport> transport)
      : transport_(std::move(transport)) {}

  connector(connector&&) = default;
  connector& operator=(connector&&) = default;

  promise<void> open(const Transport::Handlers& handlers) {
    return transport_->Open(handlers);
  }

 private:
  std::unique_ptr<Transport> transport_;
};

}  // namespace net