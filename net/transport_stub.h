#pragma once

#include "net/transport.h"

namespace net {

class StubTransport : public Transport {
 public:
  virtual Error Open(Delegate& delegate) override;
  virtual void Close() override;
  virtual int Read(void* data, size_t len) override;
  virtual int Write(const void* data, size_t len) override;
  virtual std::string GetName() const override;
  virtual bool IsMessageOriented() const override;
  virtual bool IsConnected() const override;
  virtual bool IsActive() const override;
};

inline Error StubTransport::Open(Delegate& delegate) {
  return net::OK;
}

inline void StubTransport::Close() {}

inline int StubTransport::Read(void* data, size_t len) {
  return 0;
}

inline int StubTransport::Write(const void* data, size_t len) {
  return 0;
}

inline std::string StubTransport::GetName() const {
  return "StubTransport";
}

inline bool StubTransport::IsMessageOriented() const {
  return true;
}

inline bool StubTransport::IsConnected() const {
  return false;
}

inline bool StubTransport::IsActive() const {
  return true;
}

}  // namespace net
