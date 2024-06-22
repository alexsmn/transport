#include "net/message_reader_transport.h"

#include "base/auto_reset.h"
#include "net/error.h"
#include "net/logger.h"
#include "net/message_reader.h"

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/dispatch.hpp>

namespace net {

// MessageReaderTransport::Core

struct MessageReaderTransport::Core : std::enable_shared_from_this<Core> {
  Core(std::unique_ptr<Transport> child_transport,
       std::unique_ptr<MessageReader> message_reader,
       std::shared_ptr<const Logger> logger)
      : executor_{child_transport->GetExecutor()},
        child_transport_{std::move(child_transport)},
        message_reader_{std::move(message_reader)},
        logger_{std::move(logger)} {}

  [[nodiscard]] awaitable<Error> Open();
  void Close();

  [[nodiscard]] awaitable<ErrorOr<size_t>> ReadMessage(std::span<char> buffer);

  [[nodiscard]] awaitable<ErrorOr<size_t>> WriteMessage(
      std::span<const char> data);

  Executor executor_;
  std::unique_ptr<Transport> child_transport_;
  const std::unique_ptr<MessageReader> message_reader_;
  const std::shared_ptr<const Logger> logger_;

  bool opened_ = false;

  bool reading_ = false;
  // TODO: Move into Context.
  std::vector<char> read_buffer_ =
      std::vector<char>(message_reader_->message().capacity);

  // TODO: Remove and replace with `weak_from_this`.
  std::shared_ptr<bool> cancelation_;
};

// MessageReaderTransport

MessageReaderTransport::MessageReaderTransport(
    std::unique_ptr<Transport> child_transport,
    std::unique_ptr<MessageReader> message_reader,
    std::shared_ptr<const Logger> logger)
    : core_{std::make_shared<Core>(std::move(child_transport),
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

awaitable<Error> MessageReaderTransport::Open() {
  co_return co_await core_->Open();
}

void MessageReaderTransport::Close() {
  boost::asio::dispatch(core_->executor_, std::bind_front(&Core::Close, core_));
}

bool MessageReaderTransport::IsMessageOriented() const {
  return true;
}

[[nodiscard]] awaitable<Error> MessageReaderTransport::Core::Open() {
  // Passive transport can be connected.
  // assert(!child_transport_->IsConnected());
  assert(!cancelation_);
  assert(!opened_);

  cancelation_ = std::make_shared<bool>(false);
  opened_ = true;

  auto ref = shared_from_this();

  co_return co_await child_transport_->Open();
}

void MessageReaderTransport::Core::Close() {
  if (!opened_) {
    return;
  }

  opened_ = false;
  cancelation_ = nullptr;
  message_reader_->Reset();
  child_transport_->Close();
}

awaitable<ErrorOr<std::unique_ptr<Transport>>>
MessageReaderTransport::Accept() {
  auto core = core_;

  // TODO: Bind message reader to the accepted transport.
  NET_ASSIGN_OR_CO_RETURN(auto accepted_child_transport,
                          co_await core->child_transport_->Accept());

  co_return std::make_unique<MessageReaderTransport>(
      std::move(accepted_child_transport),
      std::unique_ptr<MessageReader>{core->message_reader_->Clone()},
      core->logger_);
}

awaitable<ErrorOr<size_t>> MessageReaderTransport::Read(std::span<char> data) {
  co_return co_await core_->ReadMessage(data);
}

awaitable<ErrorOr<size_t>> MessageReaderTransport::Core::ReadMessage(
    std::span<char> buffer) {
  if (!child_transport_) {
    co_return ERR_INVALID_HANDLE;
  }

  if (!opened_) {
    co_return ERR_CONNECTION_CLOSED;
  }

  if (reading_) {
    co_return ERR_IO_PENDING;
  }

  auto ref = shared_from_this();
  auto cancelation = std::weak_ptr{cancelation_};
  base::AutoReset reading{&reading_, true};

  for (;;) {
    auto bytes_popped = message_reader_->Pop(buffer);

    if (!bytes_popped.ok()) {
      // TODO: Add UT.
      // TODO: Print message.
      logger_->Write(LogSeverity::Warning, "Invalid message");
      opened_ = false;
      co_return bytes_popped;
    }

    if (*bytes_popped != 0) {
      co_return bytes_popped;
    }

    // Don't allow composite message to contain partial messages.
    if (!message_reader_->IsEmpty() && child_transport_->IsMessageOriented()) {
      // TODO: Print message.
      logger_->Write(LogSeverity::Warning,
                     "Composite message contains a partial message");
      co_return ERR_FAILED;
    }

    auto bytes_read =
        co_await child_transport_->Read(message_reader_->Prepare());

    if (cancelation.expired()) {
      co_return ERR_ABORTED;
    }

    if (!bytes_read.ok() || *bytes_read == 0) {
      opened_ = false;
      co_return bytes_read;
    }

    message_reader_->BytesRead(*bytes_read);
  }
}

awaitable<ErrorOr<size_t>> MessageReaderTransport::Write(
    std::span<const char> data) {
  co_return co_await core_->WriteMessage(std::move(data));
}

awaitable<ErrorOr<size_t>> MessageReaderTransport::Core::WriteMessage(
    std::span<const char> data) {
  if (!child_transport_) {
    co_return ERR_INVALID_HANDLE;
  }

  co_return co_await child_transport_->Write(std::move(data));
}

std::string MessageReaderTransport::GetName() const {
  return "MSG:" + core_->child_transport_->GetName();
}

Executor MessageReaderTransport::GetExecutor() const {
  return core_->child_transport_->GetExecutor();
}

}  // namespace net
