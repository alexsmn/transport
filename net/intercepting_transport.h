#pragma once

#include "net/delegating_transport.h"
#include "net/transport_interceptor.h"

namespace net {

class InterceptingTransport : public DelegatingTransport {
 public:
  InterceptingTransport(std::unique_ptr<Transport> underlying_transport,
                        TransportInterceptor& interceptor)
      : DelegatingTransport{*underlying_transport},
        underlying_transport_{std::move(underlying_transport)},
        interceptor_{interceptor} {}

  virtual awaitable<ErrorOr<size_t>> Write(
      std::span<const char> data) override {
    if (auto intercepted = interceptor_.InterceptWrite(data)) {
      co_return std::move(*intercepted);
    }

    co_return co_await DelegatingTransport::Write(data);
  }

 private:
  std::unique_ptr<Transport> underlying_transport_;
  TransportInterceptor& interceptor_;
};

}  // namespace net