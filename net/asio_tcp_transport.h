#pragma once

#include "net/transport.h"

#include <boost/asio.hpp>
#include <boost/circular_buffer.hpp>
#include <memory>

namespace net {

class AsioTcpTransport : public Transport {
 public:
  explicit AsioTcpTransport(boost::asio::io_context& io_context);
  ~AsioTcpTransport();

  // Transport overrides
  virtual Error Open() override;
  virtual void Close() override;
  virtual int Read(void* data, size_t len) override;
  virtual int Write(const void* data, size_t len) override;
  virtual std::string GetName() const override;
  virtual bool IsMessageOriented() const override;
  virtual bool IsConnected() const override;
  virtual bool IsActive() const override { return active; }

  std::string host;
  std::string service;
  bool active = false;

 private:
  class Core;

  std::shared_ptr<Core> core_;
};

} // namespace net
