#pragma once

#include "transport/executor.h"
#include "transport/transport.h"

#include <memory>

namespace transport {

class Logger;
class MessageReader;

// A message-oriented transport constructed from a child transport using a
// provided message reader.
//
// Supports both message and stream child transports. When message child
// transport is used, the message reader is applied on concatenated child
// transport messages.
class MessageReaderTransport final : public Transport {
 public:
  MessageReaderTransport(std::unique_ptr<Transport> child_transport,
                         std::unique_ptr<MessageReader> message_reader,
                         std::shared_ptr<const Logger> logger);
  virtual ~MessageReaderTransport();

  MessageReader& message_reader();

  // Transport
  [[nodiscard]] virtual awaitable<Error> Open() override;
  [[nodiscard]] virtual awaitable<Error> Close() override;

  [[nodiscard]] virtual awaitable<ErrorOr<std::unique_ptr<Transport>>> Accept()
      override;

  [[nodiscard]] virtual awaitable<ErrorOr<size_t>> Read(
      std::span<char> data) override;

  [[nodiscard]] virtual awaitable<ErrorOr<size_t>> Write(
      std::span<const char> data) override;

  virtual std::string GetName() const override;
  virtual bool IsMessageOriented() const override;
  virtual bool IsConnected() const override;
  virtual bool IsActive() const override;
  virtual Executor GetExecutor() const override;

 private:
  struct Core;

  const std::shared_ptr<Core> core_;
};

}  // namespace transport
