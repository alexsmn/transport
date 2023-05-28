#include "net/session.h"

#include "net/bytebuf.h"
#include "net/logger.h"
#include "net/message_code.h"
#include "net/message_transport.h"
#include "net/message_utils.h"
#include "net/session_message_reader.h"

#include <algorithm>
#include <cassert>

using namespace std::chrono_literals;

namespace net {

const size_t kMaxProtocolMessage = kMaxMessage - 64;
// |kMaxSendingCount| * |MAX_MSG| must be less then size of socket send buffer.
const size_t kMaxSendingCount = 50;
const size_t kMaxAcknowledgeCount = 8;

inline bool MessageIdLessEq(uint16_t left, uint16_t right) {
  return (right - left) < static_cast<uint16_t>(-1) / 2;
}

inline bool MessageIdLess(uint16_t left, uint16_t right) {
  return (right != left) && MessageIdLessEq(left, right);
}

// Session

Session::Session(boost::asio::io_service& io_service)
    : io_service_{io_service},
      logger_(NullLogger::GetInstance()),
      timer_{io_service},
      reconnection_period_{1s} {
  timer_.StartRepeating(50ms, [this] { OnTimer(); });
}

Session::~Session() {
  Close();

  for (SessionMap::iterator i = accepted_sessions_.begin();
       i != accepted_sessions_.end();) {
    Session& session = *(i++)->second;
    session.OnClosed(ERR_CONNECTION_CLOSED);
  }
  assert(accepted_sessions_.empty());

  // Delete all accepted spare sessions.
  while (!child_sessions_.empty())
    delete *child_sessions_.begin();

  for (int i = 0; i < std::size(send_queues_); ++i) {
    MessageQueue& send_queue = send_queues_[i];
    for (MessageQueue::iterator i = send_queue.begin(); i != send_queue.end();
         ++i)
      delete[] i->data;
    send_queue.clear();
  }

  for (SendingMessageQueue::iterator i = sending_messages_.begin();
       i != sending_messages_.end(); ++i) {
    delete[] i->data;
  }
  sending_messages_.clear();

  sequence_message_.clear();

  if (parent_session_) {
    assert(parent_session_->child_sessions_.find(this) !=
           parent_session_->child_sessions_.end());

    parent_session_->child_sessions_.erase(this);
  }
}

void Session::Close() {
  if (state_ == OPENED && parent_session_) {
    assert(parent_session_->accepted_sessions_.find(id_) !=
           parent_session_->accepted_sessions_.end());
    assert(parent_session_->accepted_sessions_.find(id_)->second == this);

    parent_session_->accepted_sessions_.erase(id_);
  }

  state_ = CLOSED;
  CloseTransport();
}

void Session::CloseTransport() {
  if (state_ != CLOSED && transport_.get() && transport_->IsActive() &&
      transport_->IsConnected())
    SendClose();

  connecting_ = false;

  if (transport_.get())
    transport_->Close();

  // WARNING: |context_| may become null inside |SendClose()| above.
  cancelation_ = nullptr;
}

void Session::SetTransport(std::unique_ptr<Transport> transport) {
  if (transport_ == transport)
    return;

  // TODO: Reset sent messages.
  CloseTransport();

  if (transport && !transport->IsMessageOriented()) {
    transport = std::make_unique<MessageTransport>(
        std::move(transport), std::make_unique<SessionMessageReader>(),
        logger_);
  }

  transport_ = std::move(transport);
  if (transport_) {
    cancelation_ = std::make_shared<bool>(false);
    transport_->Open(
        {.on_open = [this] { OnTransportOpened(); },
         .on_close = [this](net::Error error) { OnTransportClosed(error); },
         .on_data = [this] { OnTransportDataReceived(); },
         .on_message =
             [this](std::span<const char> data) {
               OnTransportMessageReceived(data);
             },
         .on_accept =
             [this](std::unique_ptr<Transport> transport) {
               return OnTransportAccepted(std::move(transport));
             }});
  }
}

std::unique_ptr<Transport> Session::DetachTransport() {
  /*std::unique_ptr<Transport> transport(std::move(transport_));
  if (transport)
    transport->set_delegate(NULL);

  context_->Destroy();
  context_ = NULL;

  return transport;*/

  assert(false);
  return nullptr;
}

void Session::PostMessage(const void* data,
                          size_t size,
                          bool seq,
                          int priority) {
  assert(size > 0);
  assert(size <= kMaxProtocolMessage);

  Message message;
  message.seq = seq;
  message.data = new char[size];
  message.size = size;
  memcpy(message.data, data, size);

  MessageQueue& send_queue = send_queues_[priority ? 1 : 0];
  send_queue.push_back(message);

  SendQueuedMessage();
}

bool Session::IsSendPossible() const {
  return sending_messages_.size() < kMaxSendingCount;
}

void Session::Send(const void* data, size_t len, int priority) {
  assert(len >= 2);

  if (len <= kMaxProtocolMessage) {
    PostMessage(data, len, false, priority);
    return;
  }

  ByteBuffer<kMaxProtocolMessage> message;

  // data
  const uint8_t* ptr = reinterpret_cast<const uint8_t*>(data);
  while (len) {
    message.Clear();

    size_t count = std::min(message.max_write(), len);
    message.Write(ptr, count);
    len -= count;
    ptr += count;

    // All messages exclude last marked as "sequence".
    bool seq = len != 0;
    PostMessage(message.data, message.size, seq, priority);
  }
}

void Session::OnClosed(Error error) {
  logger_->WriteF(LogSeverity::Warning, "Session fatal error %s",
                  ErrorToString(error).c_str());

  Close();

  if (handlers_.on_close)
    handlers_.on_close(error);
  else if (accepted_)
    delete this;
}

void Session::OnSessionRestored() {
  assert(state_ == OPENED);

  if (session_transport_observer_)
    session_transport_observer_->OnSessionRecovered();
}

void Session::OnTransportError(Error error) {
  logger_->WriteF(LogSeverity::Warning, "Session transport error - %s",
                  ErrorToString(error).c_str());

  cancelation_ = nullptr;

  if (state_ == OPENED) {
    if (session_transport_observer_)
      session_transport_observer_->OnSessionTransportError(error);

  } else {
    // Signal fatal error.
    OnClosed(error);
  }
}

void Session::Open(const Handlers& handlers) {
  assert(transport_.get());
  assert(state_ == CLOSED);
  assert(!cancelation_);

  logger_->Write(LogSeverity::Normal, "Opening session");

  handlers_ = handlers;
  state_ = OPENING;
  StartConnecting();
}

int Session::Read(std::span<char> data) {
  assert(false);
  return ERR_NOT_IMPLEMENTED;
}

promise<size_t> Session::Write(std::span<const char> data) {
  Send(data.data(), data.size());
  return make_resolved_promise<size_t>(data.size());
}

std::string Session::GetName() const {
  return "Session";
}

void Session::StartConnecting() {
  assert(transport_.get());
  assert(!cancelation_);

  logger_->WriteF(LogSeverity::Normal, "Connecting to %s",
                  transport_->GetName().c_str());

  connect_start_ticks_ = Clock::now();
  connecting_ = true;
  cancelation_ = std::make_shared<bool>(false);

  transport_->Open(
      {.on_open = [this] { OnTransportOpened(); },
       .on_close = [this](net::Error error) { OnTransportClosed(error); },
       .on_data = [this] { OnTransportDataReceived(); },
       .on_message =
           [this](std::span<const char> data) {
             OnTransportMessageReceived(data);
           },
       .on_accept =
           [this](std::unique_ptr<Transport> transport) {
             return OnTransportAccepted(std::move(transport));
           }});
}

void Session::SendQueuedMessage() {
  if (!transport_.get() || !transport_->IsConnected())
    return;

  if (!cancelation_)
    return;

  std::weak_ptr<bool> cancelation = cancelation_;

  // Repeat sending messages after reconnect.
  if (repeat_sending_messages_) {
    repeat_sending_messages_ = false;
    for (auto i = sending_messages_.begin();
         !cancelation.expired() && i != sending_messages_.end(); ++i) {
      const SendingMessage& message = *i;
      SendDataMessage(message);
    }
  }

  // send
  while (!cancelation.expired() && IsSendPossible()) {
    MessageQueue* send_queue = NULL;
    for (int i = 0; i < std::size(send_queues_); ++i) {
      if (!send_queues_[i].empty()) {
        send_queue = &send_queues_[i];
        break;
      }
    }
    if (!send_queue)
      return;

    SendingMessage message;
    static_cast<Message&>(message) = send_queue->front();
    message.send_id = send_id_++;
    sending_messages_.push_back(message);

    send_queue->pop_front();

    // Sent message will be acknowledgement for all received messages.
    num_recv_ = 0;

    SendDataMessage(message);
  }
}

void Session::ProcessSessionAck(uint16_t ack) {
  while (!sending_messages_.empty() &&
         MessageIdLess(sending_messages_.front().send_id, ack)) {
    SendingMessage& message = sending_messages_.front();
    delete[] message.data;
    sending_messages_.pop_front();
  }

  SendQueuedMessage();
}

void Session::ProcessSessionMessage(uint16_t id,
                                    bool seq,
                                    const void* data,
                                    size_t len) {
  if (id != recv_id_)
    return;

  ++recv_id_;
  if (!num_recv_)
    receive_time_ = Clock::now();
  ++num_recv_;

  // handle received message
  if (seq) {
    // Sequence message.
    sequence_message_.insert(sequence_message_.end(),
                             static_cast<const char*>(data),
                             static_cast<const char*>(data) + len);

  } else if (!sequence_message_.empty()) {
    // End of sequence.
    sequence_message_.insert(sequence_message_.end(),
                             static_cast<const char*>(data),
                             static_cast<const char*>(data) + len);

    // Copy message to local to allow access to |sequence_message_| member
    // inside |Delegate::OnTransportMessageReceived()|.
    std::vector<char> sequence_message;
    sequence_message.swap(sequence_message_);

    if (handlers_.on_message) {
      handlers_.on_message(sequence_message);
    }

  } else {
    // Short message.
    if (handlers_.on_message) {
      handlers_.on_message({static_cast<const char*>(data), len});
    }
  }

  // Acknowledgement goes on OnTimer() now to allow other tasks to be performed.
}

void Session::OnTransportOpened() {
  assert(connecting_);
  assert(transport());

  logger_->WriteF(LogSeverity::Normal, "Transport opened. Name is %s",
                  transport()->GetName().c_str());

  connecting_ = false;

  if (!transport()->IsActive())
    return;

  repeat_sending_messages_ = true;

  if (accepted_)
    return;

  // If session is opened, try to restore. Otherwise, start login.
  if (state_ == OPENED) {
    logger_->Write(LogSeverity::Normal, "Restoring session");
    SendOpen(id_);
  } else {
    logger_->Write(LogSeverity::Normal, "Creating new session");
    SendCreate(create_info_);
  }
}

void Session::SendPossible() {
  SendQueuedMessage();
}

void Session::OnTimer() {
  assert(transport_.get());

  if (!transport_->IsConnected() && !connecting_ && state_ == OPENED &&
      !accepted_ &&
      Clock::now() - connect_start_ticks_ >= reconnection_period_) {
    StartConnecting();
    return;
  }

  if (!transport_->IsConnected())
    return;

  // Acknowledge received messages.
  if (num_recv_) {
    if (num_recv_ >= kMaxAcknowledgeCount ||
        Clock::now() - receive_time_ >= 1s) {
      receive_time_ = Clock::now();
      num_recv_ = 0;
      SendAck(recv_id_);
    }
  }
}

void Session::OnCreateResponse(const SessionID& session_id,
                               const SessionInfo& session_info) {
  id_ = session_id;
  session_info_ = session_info;
  state_ = OPENED;

  if (handlers_.on_open)
    handlers_.on_open();
}

void Session::OnTransportClosed(Error error) {
  logger_->WriteF(LogSeverity::Warning, "Transport closed with error %s",
                  ErrorToString(error).c_str());

  OnTransportError(error);
}

void Session::OnTransportDataReceived() {
  assert(false);
}

void Session::OnTransportMessageReceived(std::span<const char> data) {
  // TODO: Handle another size.
  assert(data.size() >= 2);

  num_bytes_received_ += data.size();
  num_messages_received_++;

  OnMessageReceived(data.data() + 2, data.size() - 2);
}

void Session::OnMessageReceived(const void* data, size_t size) {
  ByteMessage msg(const_cast<void*>(data), size, size);

  unsigned fun = msg.ReadByte();

  switch (fun) {
    case NETS_CREATE: {
      CreateSessionInfo create_info;
      create_info.name = ReadMessageString(msg);
      create_info.password = ReadMessageString(msg);
      create_info.force = msg.ReadByte() != 0;
      OnCreate(create_info);
      break;
    }

    case NETS_OPEN: {
      SessionID session_id = msg.ReadT<SessionID>();
      OnRestore(session_id);
      break;
    }

    case NETS_CLOSE: {
      // Close session request.
      logger_->Write(LogSeverity::Warning, "Close Session request");
      // Don't respond on this type of message.
      // Actual close. Assume current transport object may be deleted after next
      // call.
      OnClosed(OK);
      break;
    }

    case NETS_CREATE | NETS_RESPONSE: {
      // Client-only: Login response.
      // Parse
      Error error = static_cast<Error>(msg.ReadLong());

      logger_->WriteF(LogSeverity::Normal, "Create session response - %s",
                      ErrorToString(error).c_str());

      // Check login is failed and throw session failure.
      if (error != OK) {
        OnClosed(error);
        return;
      }

      SessionID session_id = msg.ReadT<SessionID>();
      SessionInfo session_info;
      session_info.user_id = msg.ReadLong();
      session_info.user_rights = msg.ReadLong();
      // process
      OnCreateResponse(session_id, session_info);
      break;
    }

    case NETS_OPEN | NETS_RESPONSE: {
      Error error = static_cast<Error>(msg.ReadLong());

      logger_->WriteF(LogSeverity::Normal, "Restore session response - %s",
                      ErrorToString(error).c_str());

      if (error != OK) {
        OnClosed(error);
        return;
      }

      // transport restored
      OnSessionRestored();
      break;
    }

    case NETS_MESSAGE:
    case NETS_SEQUENCE: {
      uint16_t id = msg.ReadWord();
      uint16_t ack = msg.ReadWord();
      bool seq = fun == NETS_SEQUENCE;

      // Save session context for case if session will be destroyed during
      // |ProcessSessionMessage()| call.
      std::weak_ptr<bool> cancelation = cancelation_;

      ProcessSessionMessage(id, seq, msg.ptr(), msg.max_read());

      if (!cancelation.expired())
        ProcessSessionAck(ack);
      break;
    }

    case NETS_ACK: {
      uint16_t ack = msg.ReadWord();
      ProcessSessionAck(ack);
      break;
    }

    default:
      logger_->WriteF(LogSeverity::Error, "Unknown session message %d",
                      static_cast<int>(fun));
      OnClosed(ERR_FAILED);
      break;
  }
}

void Session::SendInternal(const void* data, size_t size) {
  assert(transport());
  assert(transport()->IsConnected());

  num_bytes_sent_ += size;
  num_messages_sent_++;

  // Ignores result.
  transport()->Write({static_cast<const char*>(data), size});
}

void Session::SendAck(uint16_t recv_id) {
  ByteBuffer<> msg;
  msg.WriteWord(3);
  msg.WriteByte(NETS_ACK);
  msg.WriteWord(recv_id);
  SendInternal(msg.data, msg.size);
}

void Session::SendCreate(const CreateSessionInfo& create_info) {
  ByteBuffer<> msg;
  uint16_t& size = *(uint16_t*)msg.ptr();
  msg.Write(NULL, 2);
  msg.WriteByte(NETS_CREATE);
  WriteMessageString(msg, create_info.name);
  WriteMessageString(msg, create_info.password);
  msg.WriteByte(create_info.force ? 1 : 0);
  size = (uint16_t)msg.size - 2;
  SendInternal(msg.data, msg.size);
}

void Session::SendOpen(const SessionID& session_id) {
  ByteBuffer<> msg;
  msg.WriteWord(static_cast<uint16_t>(1 + session_id.size()));
  msg.WriteByte(NETS_OPEN);
  msg.Write(session_id.data(), session_id.size());
  SendInternal(msg.data, msg.size);
}

void Session::SendClose() {
  // NOTE: SendClose() is called before session close. Even socket maybe closed
  // at that moment, but we doesn't know about it. We mustn't handle errors in
  // such case.

  assert(transport_.get());
  assert(transport_->IsConnected());

  ByteBuffer<> msg;
  msg.WriteWord(1);
  msg.WriteByte(NETS_CLOSE);

  num_bytes_sent_ += msg.size;
  num_messages_sent_++;

  transport_->Write({reinterpret_cast<const char*>(msg.data), msg.size});
}

void Session::SendDataMessage(const SendingMessage& message) {
  assert(transport());
  assert(transport()->IsConnected());

  assert(message.size > 0);

  // Send.
  ByteBuffer<kMaxMessage> msg;
  msg.WriteWord(5 + static_cast<uint16_t>(message.size));
  msg.WriteByte(
      static_cast<uint8_t>(message.seq ? NETS_SEQUENCE : NETS_MESSAGE));
  msg.WriteWord(message.send_id);
  msg.WriteWord(recv_id_);
  msg.Write(message.data, message.size);
  SendInternal(msg.data, msg.size);
}

net::Error Session::OnTransportAccepted(std::unique_ptr<Transport> transport) {
  auto session = std::make_unique<Session>(io_service_);
  session->accepted_ = true;
  session->parent_session_ = this;
  child_sessions_.insert(session.release());

  session->OnAccepted(std::move(transport));
  return net::OK;
}

void Session::OnAccepted(std::unique_ptr<Transport> transport) {
  SetTransport(std::move(transport));
}

void Session::OnCreate(const CreateSessionInfo& create_info) {
  logger_->WriteF(LogSeverity::Normal,
                  "Create Session request name=%s force=%d",
                  create_info.name.c_str(), create_info.force ? 1 : 0);

  // process request
  SessionID session_id;
  SessionInfo session_info;
  Error error = OK;

  std::unique_ptr<net::Transport> self(this);

  if (!parent_session_ || !parent_session_->handlers_.on_accept) {
    error = ERR_FAILED;

  } else {
    create_info_ = create_info;
    // TODO: Refactor!
    error = parent_session_->handlers_.on_accept(std::move(self));
    if (error == OK) {
      state_ = OPENED;

      do {
        id_ = CreateSessionID();
      } while (
          !parent_session_->accepted_sessions_.try_emplace(id_, this).second);

      session_id = id_;
      session_info = this->session_info();
    }
  }

  // response
  {
    ByteBuffer<> msg;
    msg.WriteWord(5 + sizeof(id_) + sizeof(session_info.user_id) +
                  sizeof(session_info.user_rights));
    msg.WriteByte(NETS_CREATE | NETS_RESPONSE);
    msg.WriteLong(error);
    msg.WriteT(session_id);
    msg.WriteLong(session_info.user_id);
    msg.WriteLong(session_info.user_rights);
    transport()->Write({reinterpret_cast<const char*>(msg.data), msg.size});
  }

  // Send pending messages. Shall be sent after create session response.
  SendPossible();

  self.release();
}

void Session::OnRestore(const SessionID& session_id) {
  logger_->Write(LogSeverity::Normal, "Restore Session request");

  // process request
  Session* new_session = NULL;
  Transport* transport = this->transport();
  SessionInfo session_info;
  memset(&session_info, 0, sizeof(session_info));
  Error error = OK;

  if (!parent_session_) {
    error = ERR_FAILED;

  } else {
    Session::SessionMap::iterator i =
        parent_session_->accepted_sessions_.find(session_id);
    if (i == parent_session_->accepted_sessions_.end()) {
      error = ERR_CONNECTION_CLOSED;

    } else {
      Session& existing_session = *i->second;
      assert(existing_session.state_ == OPENED);
      assert(existing_session.accepted_);
      assert(existing_session.parent_session_ == parent_session_);

      existing_session.SetTransport(DetachTransport());

      new_session = &existing_session;
      session_info = existing_session.session_info();
      transport = existing_session.transport();

      delete this;
    }
  }

  // response
  assert(transport);
  {
    ByteBuffer<> msg;
    msg.WriteWord(13);
    msg.WriteByte(NETS_OPEN | NETS_RESPONSE);
    msg.WriteLong(error);
    msg.WriteLong(session_info.user_id);
    msg.WriteLong(session_info.user_rights);
    // Ignores promise.
    transport->Write({reinterpret_cast<const char*>(msg.data), msg.size});
  }

  // Send pending messages. Shall be sent after restore session response.
  if (new_session)
    new_session->SendPossible();
}

}  // namespace net
