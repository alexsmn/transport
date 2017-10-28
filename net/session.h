#pragma once

#include "base/memory/ref_counted.h"
#include "base/time/time.h"
#include "net/base/net_errors.h"
#include "net/session_info.h"
#include "net/timer.h"
#include "net/transport.h"

#include <deque>
#include <map>
#include <set>
#include <vector>

namespace net {

class Logger;

class NET_EXPORT Session : public Transport,
                           private Transport::Delegate {
 public:
  class SessionTransportObserver {
   public:
    virtual void OnSessionRecovered() = 0;
    virtual void OnSessionTransportError(Error error) = 0;
  };

  explicit Session(boost::asio::io_service& io_service);
  virtual ~Session();
  
  // Assign new session transport. If there is another one, delete it.
  // Session must be closed.
  void SetTransport(std::unique_ptr<Transport> transport);

  void set_session_info(const SessionInfo& session_info) { session_info_ = session_info; }
  void set_reconnection_period(unsigned period) { reconnection_period_ = period; }

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
  
  size_t send_queue_size() const { return send_queues_[0].size() + send_queues_[1].size(); }
  size_t num_bytes_received() const { return num_bytes_received_; }
  size_t num_bytes_sent() const { return num_bytes_sent_; }
  size_t num_messages_received() const { return num_messages_received_; }
  size_t num_messages_sent() const { return num_messages_sent_; }

  void Send(const void* data, size_t len, int priority = 0);

  // Get transport and reset current session state.
  std::unique_ptr<Transport> DetachTransport();

  // Transport
  virtual Error Open() override;
  virtual void Close() override;
  virtual int Read(void* data, size_t len) override;
  virtual int Write(const void* data, size_t len) override;
  virtual std::string GetName() const override;
  virtual bool IsMessageOriented() const override { return true; }
  virtual bool IsConnected() const override { return transport_ && transport_->IsConnected(); }
  virtual bool IsActive() const override { return true; }

 private:
  struct Message {
    bool seq;
    size_t size;
    char* data;
  };

  // Helper class holding information if Session is destroyed during processing
  // of |OnMessageReceived()| callback.
  class Context : public base::RefCounted<Context> {
   public:
    Context() : destroyed_(false) {}

    bool is_destroyed() const { return destroyed_; }

    void Destroy() { destroyed_ = true; }

   private:
    bool destroyed_;
  };

  typedef std::deque<Message> MessageQueue;

  struct SendingMessage : public Message {
    uint16_t send_id;
  };
  
  typedef std::deque<SendingMessage> SendingMessageQueue;
  
  struct SessionIDLess {
    bool operator()(const SessionID& left, const SessionID& right) const {
      return memcmp(&left, &right, sizeof(left)) < 0;
    }
  };
  
  typedef std::map<SessionID, Session*, SessionIDLess> SessionMap;

  void StartConnecting();
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
  void ProcessSessionMessage(uint16_t id, bool seq, const void* data,
                             size_t len);
  // Output session-level messages were acknowledged by remote side.
  void ProcessSessionAck(uint16_t id);
  // Session is created or restored - send possible.
  void SendPossible();
  // Non-restorable error happen. Session shall be closed. Fires also on any unhandled exception,
  // so must be handled.
  void OnClosed(Error error);

  void OnTimer();

  // Transport::Delegate overrides
  virtual void OnTransportOpened() override;
  virtual void OnTransportClosed(Error error) override;
  virtual void OnTransportDataReceived() override;
  virtual net::Error OnTransportAccepted(std::unique_ptr<Transport> transport) override;
  virtual void OnTransportMessageReceived(const void* data, size_t size) override;

  boost::asio::io_service& io_service_;

  SessionID id_;
  Session* parent_session_;
  CreateSessionInfo	create_info_;
  SessionInfo session_info_;
  // Login is completed. If |parent_session_| is not NULL, this session is
  // contained inside |parent_session_->accepted_sessions_|.
  enum State { CLOSED, OPENING, OPENED };
  State state_;

  // |transport_| exists during whole Session life-time from the moment of
  // |Open()|. It's not reset when underlaying transport disconnects.
  std::unique_ptr<Transport> transport_;
  
  // Id of next message to send.
  uint16_t send_id_;
  // Expected id of next received message.
  uint16_t recv_id_;
  // Sequence id for long messages sending.
  uint32_t long_send_seq_;
  
  // Time of first message of last unacked portion was received.
  base::TimeTicks receive_time_;
  base::TimeTicks connect_start_ticks_;
  
  size_t num_recv_;

  // Messages waiting to be sent.
  MessageQueue send_queues_[2];

  // Currently sending unacknowledged messages.
  SendingMessageQueue sending_messages_;

  // Reconnection occured. All |sending_messages_| shall be resent on next
  // SendQueuedMessage().
  bool repeat_sending_messages_;

  std::vector<char> sequence_message_;

  bool accepted_;
  unsigned reconnection_period_;

  bool connecting_;
  
  SessionMap accepted_sessions_;

  typedef std::set<Session*> SessionSet;
  SessionSet child_sessions_;

  scoped_refptr<Context> context_;

  Timer timer_;

  SessionTransportObserver* session_transport_observer_;
  
  // Statistics.
  size_t num_bytes_received_;
  size_t num_bytes_sent_;
  size_t num_messages_received_;
  size_t num_messages_sent_;

  std::shared_ptr<const Logger> logger_;
};

const size_t kMaxMessage = 1024;

} // namespace net
