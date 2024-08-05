#pragma once

#include "transport/intercepting_transport.h"
#include "transport/transport_factory.h"

namespace transport {

class TransportInterceptor;

class InterceptingTransportFactory : public TransportFactory {
 public:
  explicit InterceptingTransportFactory(TransportFactory& transport_factory)
      : underlying_transport_factory_(transport_factory) {}

  void set_interceptor(TransportInterceptor* interceptor) {
    interceptor_ = interceptor;
  }

  virtual ErrorOr<any_transport> CreateTransport(
      const TransportString& transport_string,
      const Executor& executor,
      std::shared_ptr<const Logger> logger = nullptr) override {
    NET_ASSIGN_OR_RETURN(auto transport,
                         underlying_transport_factory_.CreateTransport(
                             transport_string, executor, std::move(logger)));

    if (!interceptor_) {
      return transport;
    }

    return any_transport{std::make_unique<InterceptingTransport>(
        std::move(transport), *interceptor_)};
  }

 private:
  TransportFactory& underlying_transport_factory_;
  TransportInterceptor* interceptor_ = nullptr;
};

}  // namespace transport
