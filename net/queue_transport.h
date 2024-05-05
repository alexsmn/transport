#pragma once

#include "net/base/net_export.h"
#include "net/timer.h"
#include "net/transport.h"

#include <queue>
#include <vector>

namespace net {

class NET_EXPORT QueueTransport final : public Transport {
 public:
  explicit QueueTransport(const Executor& executor);

  void SetActive(QueueTransport& peer);

  void Exec();

  // Transport

  [[nodiscard]] virtual awaitable<Error> Open(Handlers handlers) override;

  virtual void Close() override;
  virtual int Read(std::span<char> data) override;

  [[nodiscard]] virtual awaitable<ErrorOr<size_t>> Write(
      std::span<const char> data) override;

  virtual std::string GetName() const override;
  virtual bool IsMessageOriented() const override { return true; }
  virtual bool IsConnected() const override { return connected_; }
  virtual bool IsActive() const override { return active_; }
  virtual Executor GetExecutor() const override { return executor_; }

 private:
  using Message = std::vector<char>;
  using MessageQueue = std::queue<Message>;

  void OnMessage(const void* data, size_t size);
  void OnAccept(QueueTransport& transport);

  Executor executor_;

  Handlers handlers_;

  MessageQueue read_queue_;

  // For active connection where to connect on |Open()|.
  QueueTransport* peer_ = nullptr;

  Timer timer_;

  bool connected_ = false;
  bool active_ = true;
};

}  // namespace net
