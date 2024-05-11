#pragma once

#include "net/base/net_errors.h"
#include "net/session_info.h"
#include "net/timer.h"
#include "net/transport.h"

#include <chrono>
#include <deque>
#include <map>
#include <set>
#include <vector>

namespace net {

class Logger;

class NET_EXPORT Session final : public Transport {
 public:
  using Clock = std::chrono::steady_clock;
  using TimePoint = Clock::time_point;
  using Duration = Clock::duration;

  class SessionTransportObserver {
   public:
    virtual void OnSessionRecovered() = 0;
    virtual void OnSessionTransportError(Error error) = 0;
  };

  explicit Session(const Executor& executor);
  virtual ~Session();

  // Assign new session transport. If there is another one, delete it.
  // Session must be closed.
  void SetTransport(std::unique_ptr<Transport> transport);

  void set_session_info(const SessionInfo& session_info) {
    session_info_ = session_info;
  }
  void set_reconnection_period(Duration period) {
    reconnection_period_ = period;
  }

  bool is_opened() const { return state_ == OPENED; }
  Transport* transport() const { return transport_.get(); }

  void set_create_info(const CreateSessionInfo& create_info) {
    create_info_ = create_info;
  }
  const CreateSessionInfo& create_info() const { return create_info_; }

  const SessionInfo& session_info() const { return session_info_; }

  SessionTransportObserver* session_transport_observer() {
    return session_transport_observer_;
  }

  void set_session_transport_observer(SessionTransportObserver* observer) {
    session_transport_observer_ = observer;
  }

  size_t send_queue_size() const {
    return send_queues_[0].size() + send_queues_[1].size();
  }

  size_t num_bytes_received() const { return num_bytes_received_; }
  size_t num_bytes_sent() const { return num_bytes_sent_; }
  size_t num_messages_received() const { return num_messages_received_; }
  size_t num_messages_sent() const { return num_messages_sent_; }

  void Send(const void* data, size_t len, int priority = 0);

  // Get transport and reset current session state.
  std::unique_ptr<Transport> DetachTransport();

  // Transport
  [[nodiscard]] virtual awaitable<Error> Open(Handlers handlers) override;
  virtual void Close() override;

  [[nodiscard]] virtual awaitable<ErrorOr<size_t>> Read(
      std::span<char> data) override;

  [[nodiscard]] virtual awaitable<ErrorOr<size_t>> Write(
      std::span<const char> data) override;

  [[nodiscard]] virtual std::string GetName() const override;
  [[nodiscard]] virtual bool IsMessageOriented() const override { return true; }

  [[nodiscard]] virtual bool IsConnected() const override {
    return transport_ && transport_->IsConnected();
  }

  [[nodiscard]] virtual bool IsActive() const override { return true; }

  [[nodiscard]] virtual Executor GetExecutor() const override {
    return executor_;
  }

 private:
  struct Message {
    bool seq;
    size_t size;
    char* data;
  };

  using MessageQueue = std::deque<Message>;

  struct SendingMessage : public Message {
    uint16_t send_id;
  };

  using SendingMessageQueue = std::deque<SendingMessage>;

  struct SessionIDLess {
    bool operator()(const SessionID& left, const SessionID& right) const {
      return memcmp(&left, &right, sizeof(left)) < 0;
    }
  };

  using SessionMap = std::map<SessionID, Session*, SessionIDLess>;

  [[nodiscard]] awaitable<Error> Connect();
  void CloseTransport();

  void PostMessage(const void* data, size_t len, bool seq, int priority);
  bool IsSendPossible() const;
  void SendQueuedMessage();

  void OnCreate(const CreateSessionInfo& create_info);
  void OnRestore(const SessionID& session_id);

  void OnMessageReceived(const void* data, size_t size);
  // Connection failure (restorable).
  void OnTransportError(Error error);
  // Transport was broken and restored without problems.
  void OnSessionRestored();

  void OnAccepted(std::unique_ptr<Transport> transport);

  // Send data message. Takes current |recv_id_| as acknowledge number.
  void SendDataMessage(const SendingMessage& message);
  void SendAck(uint16_t recv_id);
  void SendCreate(const CreateSessionInfo& create_info);
  void SendOpen(const SessionID& session_id);
  void SendClose();
  void SendInternal(const void* data, size_t size);

  // Login request completed.
  void OnCreateResponse(const SessionID& session_id,
                        const SessionInfo& session_info);
  // Input session-level message was received.
  void ProcessSessionMessage(uint16_t id,
                             bool seq,
                             const void* data,
                             size_t len);
  // Output session-level messages were acknowledged by remote side.
  void ProcessSessionAck(uint16_t id);
  // Session is created or restored - send possible.
  void SendPossible();
  // Non-restorable error happen. Session shall be closed. Fires also on any
  // unhandled exception, so must be handled.
  void OnClosed(Error error);

  void OnTimer();

  // Transport::Delegate overrides
  void OnTransportOpened();
  void OnTransportClosed(Error error);
  net::Error OnTransportAccepted(std::unique_ptr<Transport> transport);
  void OnTransportMessageReceived(std::span<const char> data);

  Executor executor_;

  std::shared_ptr<const Logger> logger_;

  Handlers handlers_;

  SessionID id_;
  Session* parent_session_ = nullptr;
  CreateSessionInfo create_info_;
  SessionInfo session_info_;
  // Login is completed. If |parent_session_| is not NULL, this session is
  // contained inside |parent_session_->accepted_sessions_|.
  enum State { CLOSED, OPENING, OPENED };
  State state_ = CLOSED;

  // |transport_| exists during whole Session life-time from the moment of
  // |Open()|. It's not reset when underlaying transport disconnects.
  std::unique_ptr<Transport> transport_;

  // Id of next message to send.
  uint16_t send_id_ = 0;
  // Expected id of next received message.
  uint16_t recv_id_ = 0;
  // Sequence id for long messages sending.
  uint32_t long_send_seq_ = 0;

  // Time of first message of last unacked portion was received.
  TimePoint receive_time_;
  TimePoint connect_start_ticks_;

  size_t num_recv_ = 0;

  // Messages waiting to be sent.
  MessageQueue send_queues_[2];

  // Currently sending unacknowledged messages.
  SendingMessageQueue sending_messages_;

  // Reconnection occured. All |sending_messages_| shall be resent on next
  // SendQueuedMessage().
  bool repeat_sending_messages_ = false;

  std::vector<char> sequence_message_;

  bool accepted_ = false;
  Duration reconnection_period_;

  bool connecting_ = false;

  SessionMap accepted_sessions_;

  typedef std::set<Session*> SessionSet;
  SessionSet child_sessions_;

  Timer timer_;

  SessionTransportObserver* session_transport_observer_ = nullptr;

  // Statistics.
  size_t num_bytes_received_ = 0;
  size_t num_bytes_sent_ = 0;
  size_t num_messages_received_ = 0;
  size_t num_messages_sent_ = 0;

  std::shared_ptr<bool> cancelation_;
};

const size_t kMaxMessage = 1024;

}  // namespace net
