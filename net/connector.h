#pragma once

#include "net/transport.h"
#include "net/transport_util.h"

#include <memory>

namespace net {

class connector {
 public:
  explicit connector(std::unique_ptr<Transport> transport)
      : transport_(std::move(transport)) {}

  connector(connector&&) = default;
  connector& operator=(connector&&) = default;

  promise<void> open(const Transport::Handlers& handlers) {
    return transport_ ? OpenWithPromise(*transport_, handlers)
                      : make_error_promise(ERR_INVALID_HANDLE);
  }

 private:
  std::unique_ptr<Transport> transport_;
};

}  // namespace net