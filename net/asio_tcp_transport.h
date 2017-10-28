#pragma once

#include "net/transport.h"

#include <boost/asio.hpp>
#include <boost/circular_buffer.hpp>

namespace net {

class AsioTcpTransport : public Transport {
 public:
  explicit AsioTcpTransport(boost::asio::io_service& io_service);
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
  using Resolver = boost::asio::ip::tcp::resolver;
  using Socket = boost::asio::ip::tcp::socket;

  void StartReading();
  
  void ProcessError(const boost::system::error_code& ec);

  Resolver resolver_;
  Socket socket_;
  bool connected_ = false;

  boost::circular_buffer<char> read_buffer_{1024};

  bool reading_ = false;
  std::vector<char> reading_buffer_;
};

} // namespace net
