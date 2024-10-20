#pragma once

#include "transport/transport.h"

namespace transport {

class DummyTransport : public Transport {
 public:
  // Transport
  virtual awaitable<error_code> open() override;
  virtual awaitable<error_code> close() override;
  virtual awaitable<expected<any_transport>> accept() override;
  virtual awaitable<expected<size_t>> read(std::span<char> data) override;
  virtual awaitable<expected<size_t>> write(
      std::span<const char> data) override;
  virtual std::string name() const override;
  virtual bool message_oriented() const override;
  virtual bool connected() const override;
  virtual bool active() const override;
  virtual executor get_executor() override;

 private:
  bool opened_ = false;
  bool connected_ = false;
};

inline awaitable<error_code> DummyTransport::open() {
  opened_ = true;

  co_return OK;
}

inline awaitable<error_code> DummyTransport::close() {
  opened_ = false;
  connected_ = false;

  co_return OK;
}

inline awaitable<expected<any_transport>> DummyTransport::accept() {
  co_return ERR_NOT_IMPLEMENTED;
}

inline awaitable<expected<size_t>> DummyTransport::read(std::span<char> data) {
  std::ranges::fill(data, 0);
  co_return static_cast<int>(data.size());
}

inline awaitable<expected<size_t>> DummyTransport::write(
    std::span<const char> data) {
  co_return data.size();
}

inline std::string DummyTransport::name() const {
  return "DummyTransport";
}

inline bool DummyTransport::message_oriented() const {
  return true;
}

inline bool DummyTransport::connected() const {
  return connected_;
}

inline bool DummyTransport::active() const {
  return true;
}

inline executor DummyTransport::get_executor() {
  return boost::asio::system_executor{};
}

}  // namespace transport
