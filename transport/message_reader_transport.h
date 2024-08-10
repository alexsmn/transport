#pragma once

#include "transport/any_transport.h"
#include "transport/executor.h"
#include "transport/log.h"
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
  MessageReaderTransport(any_transport child_transport,
                         std::unique_ptr<MessageReader> message_reader,
                         const log_source& log = {});
  virtual ~MessageReaderTransport();

  MessageReader& message_reader();

  // Transport
  [[nodiscard]] virtual awaitable<Error> open() override;
  [[nodiscard]] virtual awaitable<Error> close() override;

  [[nodiscard]] virtual awaitable<ErrorOr<any_transport>> accept() override;

  [[nodiscard]] virtual awaitable<ErrorOr<size_t>> read(
      std::span<char> data) override;

  [[nodiscard]] virtual awaitable<ErrorOr<size_t>> write(
      std::span<const char> data) override;

  virtual std::string name() const override;
  virtual bool message_oriented() const override;
  virtual bool connected() const override;
  virtual bool active() const override;
  virtual Executor get_executor() const override;

 private:
  struct Core;

  const std::shared_ptr<Core> core_;
};

}  // namespace transport
