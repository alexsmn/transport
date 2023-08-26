#pragma once

#include "net/transport.h"
#include "net/transport_interceptor.h"

namespace net {

class InterceptingTransport : public Transport {
 public:
  InterceptingTransport(std::unique_ptr<Transport> underlying_transport,
                        TransportInterceptor& interceptor)
      : underlying_transport_{std::move(underlying_transport)},
        interceptor_{interceptor} {}

  virtual void Open(const Handlers& handlers) override {
    underlying_transport_->Open(handlers);
  }

  virtual void Close() override { underlying_transport_->Close(); }

  virtual int Read(std::span<char> data) override {
    return underlying_transport_->Read(data);
  }

  virtual promise<size_t> Write(std::span<const char> data) override {
    if (auto intercepted = interceptor_.InterceptWrite(data)) {
      return std::move(*intercepted);
    } else {
      return underlying_transport_->Write(data);
    }
  }

  virtual std::string GetName() const override {
    return underlying_transport_->GetName();
  }

  // Transport supports messages by itself without using of MessageReader.
  // If returns false, Read has to be implemented.
  virtual bool IsMessageOriented() const {
    return underlying_transport_->IsMessageOriented();
  }

  virtual bool IsConnected() const override {
    return underlying_transport_->IsConnected();
  }

  virtual bool IsActive() const override {
    return underlying_transport_->IsActive();
  }

 private:
  std::unique_ptr<Transport> underlying_transport_;
  TransportInterceptor& interceptor_;
};

}  // namespace net