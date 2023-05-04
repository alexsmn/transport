#pragma once

#include "net/base/net_export.h"
#include "net/timer.h"
#include "net/transport.h"

#include <queue>
#include <vector>

namespace net {

class NET_EXPORT QueueTransport final : public Transport {
 public:
  explicit QueueTransport(boost::asio::io_service& io_service);

  void SetActive(QueueTransport& peer);

  void Exec();

  // Transport
  virtual Error Open(const Handlers& handlers) override;
  virtual void Close() override;
  virtual int Read(std::span<char> data) override;
  virtual int Write(std::span<const char> data) override;
  virtual std::string GetName() const override;
  virtual bool IsMessageOriented() const override { return true; }
  virtual bool IsConnected() const override { return connected_; }
  virtual bool IsActive() const override { return active_; }

 private:
  typedef std::vector<char> Message;
  typedef std::queue<Message> MessageQueue;

  void OnMessage(const void* data, size_t size);
  void OnAccept(QueueTransport& transport);

  boost::asio::io_service& io_service_;

  Handlers handlers_;

  MessageQueue read_queue_;

  // For active connection where to connect on |Open()|.
  QueueTransport* peer_ = nullptr;

  Timer timer_;

  bool connected_ = false;
  bool active_ = true;
};

}  // namespace net
