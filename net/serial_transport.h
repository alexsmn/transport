#pragma once

#include "net/base/net_export.h"
#include "net/serial_port.h"
#include "net/timer.h"
#include "net/transport.h"

#include <string>

namespace net {

class NET_EXPORT SerialTransport final : public Transport {
 public:
  explicit SerialTransport(boost::asio::io_context& io_context);

  // Transport overrides
  virtual Error Open(Transport::Delegate& delegate) override;
  virtual void Close() override;
  virtual int Read(void* data, size_t len) override;
  virtual int Write(const void* data, size_t len) override;
  virtual std::string GetName() const override;
  virtual bool IsMessageOriented() const override { return false; }
  virtual bool IsConnected() const override { return connected_; }
  virtual bool IsActive() const override { return true; }

  std::string m_file_name;
  DCB m_dcb;

 private:
  void OnTimer();

  Transport::Delegate* delegate_ = nullptr;

  detail::SerialPort serial_port_;

  Timer timer_;

  bool connected_ = false;
};

}  // namespace net
