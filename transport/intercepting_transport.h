#pragma once

#include "transport/delegating_transport.h"
#include "transport/transport_interceptor.h"

namespace transport {

class InterceptingTransport : public DelegatingTransport {
 public:
  InterceptingTransport(any_transport underlying_transport,
                        TransportInterceptor& interceptor)
      : DelegatingTransport{underlying_transport_},
        underlying_transport_{std::move(underlying_transport)},
        interceptor_{interceptor} {}

  virtual awaitable<expected<size_t>> write(
      std::span<const char> data) override {
    if (auto intercepted = interceptor_.InterceptWrite(data)) {
      co_return std::move(*intercepted);
    }

    co_return co_await DelegatingTransport::write(data);
  }

 private:
  any_transport underlying_transport_;
  TransportInterceptor& interceptor_;
};

}  // namespace transport