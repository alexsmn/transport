#include "net/message_transport.h"

#include "net/base/net_errors.h"
#include "net/logger.h"
#include "net/message_reader.h"
#include "net/span.h"

namespace net {

namespace {

int ReadMessage(span<const char>& buffer,
                MessageReader& message_reader,
                const Logger& logger,
                ByteMessage& message) {
  for (;;) {
    size_t bytes_to_read = 0;
    bool ok = message_reader.GetBytesToRead(bytes_to_read);
    if (!ok) {
      if (message_reader.has_error_correction()) {
        message_reader.SkipFirstByte();
        continue;
      } else {
        logger.WriteF(LogSeverity::Warning,
                      "Can't estimate remaining message size");
        message_reader.Reset();
        return ERR_FAILED;
      }
    }

    if (bytes_to_read == 0)
      break;

    if (bytes_to_read > buffer.size()) {
      logger.WriteF(LogSeverity::Warning, "Message is too short");
      return ERR_FAILED;
    }

    memcpy(message_reader.ptr(), buffer.data(), bytes_to_read);
    message_reader.BytesRead(bytes_to_read);
    buffer = buffer.subspan(bytes_to_read);
  }

  if (!message_reader.complete()) {
    logger.WriteF(LogSeverity::Warning, "Incomplete message");
    return ERR_FAILED;
  }

  // Message is complete.
  message = message_reader.message();

  // Clear buffer.
  message_reader.Reset();
  return static_cast<int>(message.size);
}

}  // namespace

MessageTransport::MessageTransport(
    std::unique_ptr<Transport> child_transport,
    std::unique_ptr<MessageReader> message_reader,
    std::shared_ptr<const Logger> logger)
    : child_transport_(std::move(child_transport)),
      message_reader_(std::move(message_reader)),
      logger_{std::move(logger)},
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

void MessageTransport::Open(const Handlers& handlers) {
  // Passive transport can be connected.
  // assert(!child_transport_->IsConnected());
  assert(!cancelation_);

  handlers_ = handlers;
  cancelation_ = std::make_shared<bool>(false);

  child_transport_->Open(
      {.on_open = [this] { OnChildTransportOpened(); },
       .on_close = [this](net::Error error) { OnChildTransportClosed(error); },
       .on_data = [this] { OnChildTransportDataReceived(); },
       .on_message =
           [this](std::span<const char> data) {
             OnChildTransportMessageReceived(data);
           },
       .on_accept =
           [this](std::unique_ptr<Transport> transport) {
             return OnChildTransportAccepted(std::move(transport));
           }});
}

void MessageTransport::InternalClose() {
  if (!child_transport_->IsConnected())
    return;

  handlers_ = {};
  cancelation_ = nullptr;
  child_transport_->Close();
  message_reader_->Reset();
}

void MessageTransport::Close() {
  InternalClose();
}

int MessageTransport::Read(std::span<char> data) {
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
        logger_->WriteF(LogSeverity::Warning,
                        "Can't estimate remaining message size");
        message_reader_->Reset();
        return ERR_FAILED;
      }
    }

    if (bytes_to_read == 0)
      break;

    int res = child_transport_->Read(
        {static_cast<char*>(message_reader_->ptr()), bytes_to_read});
    if (res <= 0)
      return res;

    message_reader_->BytesRead(static_cast<size_t>(res));
  }

  if (!message_reader_->complete())
    return 0;

  // Message is complete.
  const ByteMessage& reader_message = message_reader_->message();
  if (reader_message.size > len) {
    logger_->WriteF(LogSeverity::Warning, "Buffer is too short");
    return ERR_FAILED;
  }

  memcpy(data, reader_message.data, reader_message.size);
  int size = reader_message.size;

  // Clear buffer.
  message_reader_->Reset();
  return size;
}

promise<size_t> MessageTransport::Write(std::span<const char> data) {
  return child_transport_ ? child_transport_->Write(data)
                          : make_error_promise<size_t>(ERR_INVALID_HANDLE);
}

std::string MessageTransport::GetName() const {
  return "MSG:" + child_transport_->GetName();
}

void MessageTransport::OnChildTransportOpened() {
  assert(child_transport_->IsConnected());

  if (handlers_.on_open)
    handlers_.on_open();
}

Error MessageTransport::OnChildTransportAccepted(
    std::unique_ptr<Transport> transport) {
  return handlers_.on_accept ? handlers_.on_accept(std::move(transport))
                             : ERR_ACCESS_DENIED;
}

void MessageTransport::OnChildTransportClosed(Error error) {
  assert(child_transport_);
  assert(cancelation_);

  cancelation_ = nullptr;

  if (handlers_.on_close)
    handlers_.on_close(error);
}

void MessageTransport::OnChildTransportDataReceived() {
  assert(child_transport_->IsConnected());

  std::weak_ptr<bool> cancelation = cancelation_;
  while (!cancelation.expired()) {
    int res = InternalRead(read_buffer_.data(), read_buffer_.size());
    if (res == 0)
      break;
    // Ignore error here. Leave it to usual error reporting logic.
    if (res < 0)
      break;

    if (handlers_.on_message)
      handlers_.on_message({read_buffer_.data(), static_cast<size_t>(res)});
  }
}

void MessageTransport::OnChildTransportMessageReceived(
    std::span<const char> data) {
  assert(child_transport_->IsMessageOriented());

  span<const char> buffer{data.data(), data.size()};

  std::weak_ptr<bool> cancelation = cancelation_;
  ByteMessage message;
  while (!cancelation.expired() && !buffer.empty()) {
    int res = ReadMessage(buffer, *message_reader_, *logger_, message);
    if (res > 0) {
      if (handlers_.on_message) {
        handlers_.on_message(
            {reinterpret_cast<const char*>(message.data), message.size});
      }
    } else if (res < 0) {
      if (message_reader_->has_error_correction()) {
        logger_->WriteF(LogSeverity::Warning,
                        "Error on message parsing - %s. Message rejected.",
                        ErrorToString(static_cast<Error>(res)).c_str());
        message_reader_->Reset();
      } else {
        logger_->WriteF(LogSeverity::Error, "Error on message parsing - %s",
                        ErrorToString(static_cast<Error>(res)).c_str());
        child_transport_->Close();
        if (handlers_.on_close) {
          handlers_.on_close(static_cast<Error>(res));
        }
      }
      return;
    }
  }
}

}  // namespace net
