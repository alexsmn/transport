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

  virtual promise<size_t> Write(std::span<const char> data) override {
    if (auto intercepted = interceptor_.InterceptWrite(data)) {
      return std::move(*intercepted);
    } else {
      return DelegatingTransport::Write(data);
    }
  }

 private:
  std::unique_ptr<Transport> underlying_transport_;
  TransportInterceptor& interceptor_;
};

}  // namespace net