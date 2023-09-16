#pragma once

#include "net/intercepting_transport.h"
#include "net/transport_factory.h"

namespace net {

class TransportInterceptor;

class InterceptingTransportFactory : public TransportFactory {
 public:
  explicit InterceptingTransportFactory(TransportFactory& transport_factory)
      : underlying_transport_factory_(transport_factory) {}

  void set_interceptor(TransportInterceptor* interceptor) {
    interceptor_ = interceptor;
  }

  virtual std::unique_ptr<Transport> CreateTransport(
      const TransportString& transport_string,
      std::shared_ptr<const Logger> logger = nullptr) override {
    auto transport = underlying_transport_factory_.CreateTransport(
        transport_string, std::move(logger));

    if (transport && interceptor_) {
      return std::make_unique<InterceptingTransport>(std::move(transport),
                                                     *interceptor_);
    } else {
      return transport;
    }
  }

 private:
  TransportFactory& underlying_transport_factory_;
  TransportInterceptor* interceptor_ = nullptr;
};

}  // namespace net
