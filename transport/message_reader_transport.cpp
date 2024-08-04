#include "transport/message_reader_transport.h"

#include "transport/auto_reset.h"
#include "transport/error.h"
#include "transport/logger.h"
#include "transport/message_reader.h"

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/dispatch.hpp>

namespace transport {

// MessageReaderTransport::Core

struct MessageReaderTransport::Core : std::enable_shared_from_this<Core> {
  Core(std::unique_ptr<Transport> child_transport,
       std::unique_ptr<MessageReader> message_reader,
       std::shared_ptr<const Logger> logger)
      : executor_{child_transport->get_executor()},
        child_transport_{std::move(child_transport)},
        message_reader_{std::move(message_reader)},
        logger_{std::move(logger)} {}

  [[nodiscard]] awaitable<Error> Open();
  [[nodiscard]] awaitable<Error> Close();

  [[nodiscard]] awaitable<ErrorOr<size_t>> ReadMessage(std::span<char> buffer);

  [[nodiscard]] awaitable<ErrorOr<size_t>> WriteMessage(
      std::span<const char> data);

  Executor executor_;
  std::unique_ptr<Transport> child_transport_;
  const std::unique_ptr<MessageReader> message_reader_;
  const std::shared_ptr<const Logger> logger_;

  bool opened_ = false;
  bool reading_ = false;

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
  // assert(!child_transport_->connected());
}

MessageReaderTransport::~MessageReaderTransport() {
  boost::asio::dispatch(core_->executor_, std::bind_front(&Core::Close, core_));
}

MessageReader& MessageReaderTransport::message_reader() {
  return *core_->message_reader_;
}

bool MessageReaderTransport::connected() const {
  return core_->child_transport_->connected();
}

bool MessageReaderTransport::active() const {
  return core_->child_transport_->active();
}

awaitable<Error> MessageReaderTransport::open() {
  return core_->Open();
}

awaitable<Error> MessageReaderTransport::close() {
  return core_->Close();
}

bool MessageReaderTransport::message_oriented() const {
  return true;
}

[[nodiscard]] awaitable<Error> MessageReaderTransport::Core::Open() {
  // Passive transport can be connected.
  // assert(!child_transport_->connected());
  assert(!cancelation_);
  assert(!opened_);

  cancelation_ = std::make_shared<bool>(false);
  opened_ = true;

  return child_transport_->open();
}

awaitable<Error> MessageReaderTransport::Core::Close() {
  if (!opened_) {
    co_return ERR_CONNECTION_CLOSED;
  }

  opened_ = false;
  cancelation_ = nullptr;
  message_reader_->Reset();

  co_return co_await child_transport_->close();
}

awaitable<ErrorOr<std::unique_ptr<Transport>>>
MessageReaderTransport::accept() {
  auto core = core_;

  // TODO: Bind message reader to the accepted transport.
  NET_ASSIGN_OR_CO_RETURN(auto accepted_child_transport,
                          co_await core->child_transport_->accept());

  co_return std::make_unique<MessageReaderTransport>(
      std::move(accepted_child_transport),
      std::unique_ptr<MessageReader>{core->message_reader_->Clone()},
      core->logger_);
}

awaitable<ErrorOr<size_t>> MessageReaderTransport::read(std::span<char> data) {
  return core_->ReadMessage(data);
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
  AutoReset reading{reading_, true};

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
    if (!message_reader_->IsEmpty() && child_transport_->message_oriented()) {
      // TODO: Print message.
      logger_->Write(LogSeverity::Warning,
                     "Composite message contains a partial message");
      co_return ERR_FAILED;
    }

    auto bytes_read =
        co_await child_transport_->read(message_reader_->Prepare());

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

awaitable<ErrorOr<size_t>> MessageReaderTransport::write(
    std::span<const char> data) {
  co_return co_await core_->WriteMessage(std::move(data));
}

awaitable<ErrorOr<size_t>> MessageReaderTransport::Core::WriteMessage(
    std::span<const char> data) {
  if (!child_transport_) {
    co_return ERR_INVALID_HANDLE;
  }

  co_return co_await child_transport_->write(std::move(data));
}

std::string MessageReaderTransport::name() const {
  return "MSG:" + core_->child_transport_->name();
}

Executor MessageReaderTransport::get_executor() const {
  return core_->child_transport_->get_executor();
}

}  // namespace transport
