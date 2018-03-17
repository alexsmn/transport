#pragma once

#include "net/transport.h"

#include <boost/asio.hpp>
#include <boost/circular_buffer.hpp>
#include <memory>

namespace net {

class AsioTcpTransport final : public Transport {
 public:
  explicit AsioTcpTransport(boost::asio::io_context& io_context);
  AsioTcpTransport(boost::asio::io_context& io_context,
                   boost::asio::ip::tcp::socket socket);
  ~AsioTcpTransport();

  // Transport overrides
  virtual Error Open(Transport::Delegate& delegate) override;
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
  class ActiveCore;
  class PassiveCore;

  boost::asio::io_context& io_context_;
  std::shared_ptr<Core> core_;
};

}  // namespace net
