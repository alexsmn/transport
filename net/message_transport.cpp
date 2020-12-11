#include "net/message_transport.h"

#include "net/base/net_errors.h"
#include "net/message_reader.h"
#include "net/span.h"

namespace net {

namespace {

int ReadMessage(span<const char>& buffer,
                MessageReader& message_reader,
                ByteMessage& message) {
  for (;;) {
    size_t bytes_to_read = 0;
    bool ok = message_reader.GetBytesToRead(bytes_to_read);
    if (!ok) {
      if (message_reader.has_error_correction()) {
        message_reader.SkipFirstByte();
        continue;
      } else {
        message_reader.Reset();
        return ERR_FAILED;
      }
    }

    if (bytes_to_read == 0)
      break;

    if (bytes_to_read > buffer.size())
      return ERR_FAILED;

    memcpy(message_reader.ptr(), buffer.data(), bytes_to_read);
    message_reader.BytesRead(bytes_to_read);
    buffer = buffer.subspan(bytes_to_read);
  }

  if (!message_reader.complete())
    return 0;

  // Message is complete.
  message = message_reader.message();

  // Clear buffer.
  message_reader.Reset();
  return static_cast<int>(message.size);
}

}  // namespace

MessageTransport::MessageTransport(
    std::unique_ptr<Transport> child_transport,
    std::unique_ptr<MessageReader> message_reader)
    : child_transport_(std::move(child_transport)),
      message_reader_(std::move(message_reader)),
      max_message_size_(message_reader_->message().capacity),
      read_buffer_(max_message_size_) {
  assert(child_transport_);
  // Passive transport can be connected.
  // assert(!child_transport_->IsConnected());
}

MessageTransport::~MessageTransport() {
  InternalClose();
}

bool MessageTransport::IsMessageOriented() const {
  return true;
}

Error MessageTransport::Open(Transport::Delegate& delegate) {
  // Passive transport can be connected.
  // assert(!child_transport_->IsConnected());
  assert(!cancelation_);

  delegate_ = &delegate;
  cancelation_ = std::make_shared<bool>(false);

  return child_transport_->Open(*this);
}

void MessageTransport::InternalClose() {
  if (!child_transport_->IsConnected())
    return;

  delegate_ = nullptr;
  cancelation_ = nullptr;
  child_transport_->Close();
  message_reader_->Reset();
}

void MessageTransport::Close() {
  InternalClose();
}

int MessageTransport::Read(void* data, size_t len) {
  assert(false);
  return ERR_UNEXPECTED;
}

int MessageTransport::InternalRead(void* data, size_t len) {
  if (!child_transport_.get())
    return ERR_INVALID_HANDLE;

  for (;;) {
    size_t bytes_to_read = 0;
    bool ok = message_reader_->GetBytesToRead(bytes_to_read);
    if (!ok) {
      if (message_reader_->has_error_correction()) {
        message_reader_->SkipFirstByte();
        continue;
      } else {
        message_reader_->Reset();
        return ERR_FAILED;
      }
    }

    if (bytes_to_read == 0)
      break;

    int res = child_transport_->Read(message_reader_->ptr(), bytes_to_read);
    if (res <= 0)
      return res;

    message_reader_->BytesRead(static_cast<size_t>(res));
  }

  if (!message_reader_->complete())
    return 0;

  // Message is complete.
  const ByteMessage& reader_message = message_reader_->message();
  if (reader_message.size > len)
    return ERR_FAILED;

  memcpy(data, reader_message.data, reader_message.size);
  int size = reader_message.size;

  // Clear buffer.
  message_reader_->Reset();
  return size;
}

int MessageTransport::Write(const void* data, size_t len) {
  if (!child_transport_)
    return ERR_INVALID_HANDLE;
  return child_transport_->Write(data, len);
}

std::string MessageTransport::GetName() const {
  return "MSG:" + child_transport_->GetName();
}

void MessageTransport::OnTransportOpened() {
  assert(child_transport_->IsConnected());

  if (delegate_)
    delegate_->OnTransportOpened();
}

net::Error MessageTransport::OnTransportAccepted(
    std::unique_ptr<Transport> transport) {
  return delegate_->OnTransportAccepted(std::move(transport));
}

void MessageTransport::OnTransportClosed(Error error) {
  assert(child_transport_);
  assert(cancelation_);

  cancelation_ = nullptr;

  if (delegate_)
    delegate_->OnTransportClosed(error);
}

void MessageTransport::OnTransportDataReceived() {
  assert(child_transport_->IsConnected());

  std::weak_ptr<bool> cancelation = cancelation_;
  while (!cancelation.expired()) {
    int res = InternalRead(read_buffer_.data(), read_buffer_.size());
    if (res == 0)
      break;
    // Ignore error here. Leave it to usual error reporting logic.
    if (res < 0)
      break;

    if (delegate_)
      delegate_->OnTransportMessageReceived(read_buffer_.data(),
                                            static_cast<size_t>(res));
  }
}

void MessageTransport::OnTransportMessageReceived(const void* data,
                                                  size_t size) {
  assert(child_transport_->IsMessageOriented());

  span<const char> buffer{static_cast<const char*>(data), size};

  std::weak_ptr<bool> cancelation = cancelation_;
  ByteMessage message;
  while (!cancelation.expired() && !buffer.empty()) {
    int res = ReadMessage(buffer, *message_reader_, message);
    if (res > 0) {
      delegate_->OnTransportMessageReceived(message.data, message.size);
    } else if (res < 0) {
      child_transport_->Close();
      delegate_->OnTransportClosed(static_cast<Error>(res));
      return;
    }
  }
}

}  // namespace net
