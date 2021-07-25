#pragma once

#include "net/transport.h"

namespace net {

class DummyTransport : public Transport {
 public:
  // Transport
  virtual Error Open(Delegate& delegate) override;
  virtual void Close() override;
  virtual int Read(void* data, size_t len) override;
  virtual int Write(const void* data, size_t len) override;
  virtual std::string GetName() const override;
  virtual bool IsMessageOriented() const override;
  virtual bool IsConnected() const override;
  virtual bool IsActive() const override;

 private:
  bool opened_ = false;
  bool connected_ = false;
  Delegate* delegate_ = nullptr;
};

inline Error DummyTransport::Open(Delegate& delegate) {
  opened_ = true;
  delegate_ = &delegate;
  delegate_->OnTransportOpened();
  return OK;
}

inline void DummyTransport::Close() {
  opened_ = false;
  connected_ = false;
  auto* delegate = delegate_;
  delegate_ = nullptr;
  delegate->OnTransportClosed(OK);
}

inline int DummyTransport::Read(void* data, size_t len) {
  return static_cast<int>(len);
}

inline int DummyTransport::Write(const void* data, size_t len) {
  return static_cast<int>(len);
}

inline std::string DummyTransport::GetName() const {
  return "DummyTransport";
}

inline bool DummyTransport::IsMessageOriented() const {
  return true;
}

inline bool DummyTransport::IsConnected() const {
  return connected_;
}

inline bool DummyTransport::IsActive() const {
  return true;
}

}  // namespace net
