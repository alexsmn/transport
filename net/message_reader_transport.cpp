#include "net/message_reader_transport.h"

#include "net/base/net_errors.h"
#include "net/logger.h"
#include "net/message_reader.h"
#include "net/net_exception.h"

#include "net/base/threading/thread_collision_warner.h"
#include <boost/asio/bind_executor.hpp>
#include <boost/asio/dispatch.hpp>

namespace net {

namespace {

int GetBytesToRead(MessageReader& message_reader, const Logger& logger) {
  for (;;) {
    size_t bytes_to_read = 0;
    bool ok = message_reader.GetBytesToRead(bytes_to_read);
    if (ok) {
      return bytes_to_read;
    }

    if (!message_reader.TryCorrectError()) {
      logger.WriteF(LogSeverity::Warning,
                    "Can't estimate remaining message size");
      message_reader.Reset();
      return ERR_FAILED;
    }
  }
}

int ReadMessage(std::span<const char>& buffer,
                MessageReader& message_reader,
                const Logger& logger,
                ByteMessage& message) {
  for (;;) {
    int bytes_to_read = GetBytesToRead(message_reader, logger);
    if (bytes_to_read < 0) {
      return bytes_to_read;
    } else if (bytes_to_read == 0) {
      break;
    }

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

// MessageReaderTransport::Core

struct MessageReaderTransport::Core : std::enable_shared_from_this<Core> {
  Core(const Executor& executor,
       std::unique_ptr<Transport> child_transport,
       std::unique_ptr<MessageReader> message_reader,
       std::shared_ptr<const Logger> logger)
      : executor_{executor},
        child_transport_{std::move(child_transport)},
        message_reader_{std::move(message_reader)},
        logger_{std::move(logger)} {}

  [[nodiscard]] awaitable<void> Open(Handlers handlers);
  void Close();

  int ReadMessage(void* data, size_t len);
  awaitable<size_t> WriteMessage(std::vector<char> data);

  // Child handlers.
  void OnChildTransportOpened();
  void OnChildTransportAccepted(std::unique_ptr<Transport> transport);
  void OnChildTransportClosed(Error error);
  void OnChildTransportDataReceived();
  void OnChildTransportMessageReceived(std::span<const char> data);

  Executor executor_;
  std::unique_ptr<Transport> child_transport_;
  const std::unique_ptr<MessageReader> message_reader_;
  const std::shared_ptr<const Logger> logger_;

  Handlers handlers_;

  DFAKE_MUTEX(mutex_);

  // TODO: Move into Context.
  std::vector<char> read_buffer_ =
      std::vector<char>(message_reader_->message().capacity);

  // TODO: Remove and replace with `weak_from_this`.
  std::shared_ptr<bool> cancelation_;
};

// MessageReaderTransport

MessageReaderTransport::MessageReaderTransport(
    const Executor& executor,
    std::unique_ptr<Transport> child_transport,
    std::unique_ptr<MessageReader> message_reader,
    std::shared_ptr<const Logger> logger)
    : core_{std::make_shared<Core>(executor,
                                   std::move(child_transport),
                                   std::move(message_reader),
                                   std::move(logger))} {
  assert(core_->child_transport_);
  // Passive transport can be connected.
  // assert(!child_transport_->IsConnected());
}

MessageReaderTransport::~MessageReaderTransport() {
  boost::asio::dispatch(core_->executor_, std::bind_front(&Core::Close, core_));
}

MessageReader& MessageReaderTransport::message_reader() {
  return *core_->message_reader_;
}

bool MessageReaderTransport::IsConnected() const {
  return core_->child_transport_->IsConnected();
}

bool MessageReaderTransport::IsActive() const {
  return core_->child_transport_->IsActive();
}

awaitable<void> MessageReaderTransport::Open(Handlers handlers) {
  return core_->Open(std::move(handlers));
}

void MessageReaderTransport::Close() {
  boost::asio::dispatch(core_->executor_, std::bind_front(&Core::Close, core_));
}

bool MessageReaderTransport::IsMessageOriented() const {
  return true;
}

[[nodiscard]] awaitable<void> MessageReaderTransport::Core::Open(
    Handlers handlers) {
  DFAKE_SCOPED_RECURSIVE_LOCK(mutex_);

  // Passive transport can be connected.
  // assert(!child_transport_->IsConnected());
  assert(!cancelation_);

  handlers_ = std::move(handlers);
  cancelation_ = std::make_shared<bool>(false);

  auto ref = shared_from_this();

  co_await child_transport_->Open(
      {.on_open = boost::asio::bind_executor(
           executor_, std::bind_front(&Core::OnChildTransportOpened, ref)),
       .on_close = boost::asio::bind_executor(
           executor_, std::bind_front(&Core::OnChildTransportClosed, ref)),
       .on_data = boost::asio::bind_executor(
           executor_,
           std::bind_front(&Core::OnChildTransportDataReceived, ref)),
       .on_message =
           [this, ref](std::span<const char> data) {
             boost::asio::dispatch(
                 executor_,
                 std::bind_front(&Core::OnChildTransportMessageReceived, ref,
                                 std::vector(data.begin(), data.end())));
           },
       .on_accept =
           [this, ref](std::unique_ptr<Transport> transport) {
             auto shared_transport =
                 std::make_shared<std::unique_ptr<Transport>>(
                     std::move(transport));
             boost::asio::dispatch(executor_, [this, ref, shared_transport] {
               OnChildTransportAccepted(std::move(*shared_transport));
             });
           }});
}

void MessageReaderTransport::Core::Close() {
  DFAKE_SCOPED_RECURSIVE_LOCK(mutex_);

  if (!child_transport_->IsConnected()) {
    return;
  }

  handlers_ = {};
  cancelation_ = nullptr;
  message_reader_->Reset();
  child_transport_->Close();
}

int MessageReaderTransport::Read(std::span<char> data) {
  assert(false);
  return ERR_UNEXPECTED;
}

int MessageReaderTransport::Core::ReadMessage(void* data, size_t len) {
  DFAKE_SCOPED_RECURSIVE_LOCK(mutex_);

  if (!child_transport_.get())
    return ERR_INVALID_HANDLE;

  for (;;) {
    int bytes_to_read = GetBytesToRead(*message_reader_, *logger_);
    if (bytes_to_read < 0) {
      return bytes_to_read;
    } else if (bytes_to_read == 0) {
      break;
    }

    int res =
        child_transport_->Read({static_cast<char*>(message_reader_->ptr()),
                                static_cast<size_t>(bytes_to_read)});
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

awaitable<size_t> MessageReaderTransport::Write(std::vector<char> data) {
  return core_->WriteMessage(std::move(data));
}

awaitable<size_t> MessageReaderTransport::Core::WriteMessage(
    std::vector<char> data) {
  DFAKE_SCOPED_RECURSIVE_LOCK(mutex_);

  if (!child_transport_) {
    throw net_exception(ERR_INVALID_HANDLE);
  }

  return child_transport_->Write(std::move(data));
}

std::string MessageReaderTransport::GetName() const {
  return "MSG:" + core_->child_transport_->GetName();
}

Executor MessageReaderTransport::GetExecutor() const {
  return core_->child_transport_->GetExecutor();
}

void MessageReaderTransport::Core::OnChildTransportOpened() {
  DFAKE_SCOPED_RECURSIVE_LOCK(mutex_);

  assert(child_transport_->IsConnected());

  if (handlers_.on_open)
    handlers_.on_open();
}

void MessageReaderTransport::Core::OnChildTransportAccepted(
    std::unique_ptr<Transport> transport) {
  DFAKE_SCOPED_RECURSIVE_LOCK(mutex_);

  if (handlers_.on_accept) {
    handlers_.on_accept(std::move(transport));
  }
}

void MessageReaderTransport::Core::OnChildTransportClosed(Error error) {
  DFAKE_SCOPED_RECURSIVE_LOCK(mutex_);

  assert(child_transport_);

  auto cancelation = std::exchange(cancelation_, nullptr);

  if (handlers_.on_close && cancelation) {
    handlers_.on_close(error);
  }
}

void MessageReaderTransport::Core::OnChildTransportDataReceived() {
  DFAKE_SCOPED_RECURSIVE_LOCK(mutex_);

  assert(child_transport_->IsConnected());

  std::weak_ptr<bool> cancelation = cancelation_;
  while (!cancelation.expired()) {
    int res = ReadMessage(read_buffer_.data(), read_buffer_.size());
    if (res == 0)
      break;
    // Ignore error here. Leave it to usual error reporting logic.
    if (res < 0)
      break;

    if (handlers_.on_message)
      handlers_.on_message({read_buffer_.data(), static_cast<size_t>(res)});
  }
}

void MessageReaderTransport::Core::OnChildTransportMessageReceived(
    std::span<const char> data) {
  DFAKE_SCOPED_RECURSIVE_LOCK(mutex_);

  assert(child_transport_->IsMessageOriented());

  std::span<const char> buffer{data.data(), data.size()};

  std::weak_ptr<bool> cancelation = cancelation_;
  ByteMessage message;
  while (!cancelation.expired() && !buffer.empty()) {
    int res = net::ReadMessage(buffer, *message_reader_, *logger_, message);
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
