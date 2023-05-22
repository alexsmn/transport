#pragma once

#include "net/transport.h"

namespace net {

class DummyTransport : public Transport {
 public:
  // Transport
  virtual void Open(const Handlers& handlers) override;
  virtual void Close() override;
  virtual int Read(std::span<char> data) override;
  virtual int Write(std::span<const char> data) override;
  virtual std::string GetName() const override;
  virtual bool IsMessageOriented() const override;
  virtual bool IsConnected() const override;
  virtual bool IsActive() const override;

 private:
  bool opened_ = false;
  bool connected_ = false;
  Handlers handlers_;
};

inline void DummyTransport::Open(const Handlers& handlers) {
  opened_ = true;
  handlers_ = handlers;

  if (auto on_open = std::move(handlers_.on_open)) {
    on_open();
  }
}

inline void DummyTransport::Close() {
  auto on_close = std::move(handlers_.on_close);

  opened_ = false;
  connected_ = false;
  handlers_ = {};

  if (on_close)
    on_close(OK);
}

inline int DummyTransport::Read(std::span<char> data) {
  std::ranges::fill(data, 0);
  return static_cast<int>(data.size());
}

inline int DummyTransport::Write(std::span<const char> data) {
  return static_cast<int>(data.size());
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
