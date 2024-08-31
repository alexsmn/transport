#pragma once

#include "transport/timer.h"
#include "transport/transport.h"

#include <queue>
#include <vector>

namespace transport {

class QueueTransport final : public Transport {
 public:
  explicit QueueTransport(const Executor& executor);

  void SetActive(QueueTransport& peer);

  void Exec();

  // Transport

  [[nodiscard]] virtual awaitable<error_code> open() override;
  [[nodiscard]] virtual awaitable<error_code> close() override;
  [[nodiscard]] virtual awaitable<expected<any_transport>> accept() override;

  [[nodiscard]] virtual awaitable<expected<size_t>> read(
      std::span<char> data) override;

  [[nodiscard]] virtual awaitable<expected<size_t>> write(
      std::span<const char> data) override;

  virtual std::string name() const override;
  virtual bool message_oriented() const override { return true; }
  virtual bool connected() const override { return connected_; }
  virtual bool active() const override { return active_; }
  virtual Executor get_executor() override { return executor_; }

 private:
  using Message = std::vector<char>;
  using MessageQueue = std::queue<Message>;

  void OnMessage(const void* data, size_t size);
  void OnAccept(QueueTransport& transport);

  Executor executor_;

  MessageQueue read_queue_;

  // For active connection where to connect on |Open()|.
  QueueTransport* peer_ = nullptr;

  Timer timer_;

  bool connected_ = false;
  bool active_ = true;
};

}  // namespace transport
