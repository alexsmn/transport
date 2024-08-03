#pragma once

#include "transport/transport.h"

namespace transport {

class DummyTransport : public Transport {
 public:
  // Transport
  virtual awaitable<Error> Open() override;
  virtual void Close() override;
  virtual awaitable<ErrorOr<std::unique_ptr<Transport>>> Accept() override;
  virtual awaitable<ErrorOr<size_t>> Read(std::span<char> data) override;
  virtual awaitable<ErrorOr<size_t>> Write(std::span<const char> data) override;
  virtual std::string GetName() const override;
  virtual bool IsMessageOriented() const override;
  virtual bool IsConnected() const override;
  virtual bool IsActive() const override;
  virtual Executor GetExecutor() const override;

 private:
  bool opened_ = false;
  bool connected_ = false;
};

inline awaitable<Error> DummyTransport::Open() {
  opened_ = true;

  co_return OK;
}

inline void DummyTransport::Close() {
  opened_ = false;
  connected_ = false;
}

inline awaitable<ErrorOr<std::unique_ptr<Transport>>> DummyTransport::Accept() {
  co_return ERR_NOT_IMPLEMENTED;
}

inline awaitable<ErrorOr<size_t>> DummyTransport::Read(std::span<char> data) {
  std::ranges::fill(data, 0);
  co_return static_cast<int>(data.size());
}

inline awaitable<ErrorOr<size_t>> DummyTransport::Write(
    std::span<const char> data) {
  co_return data.size();
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

inline Executor DummyTransport::GetExecutor() const {
  return boost::asio::system_executor{};
}

}  // namespace transport
