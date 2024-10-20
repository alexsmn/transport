#pragma once

#include "transport/awaitable.h"
#include "transport/error.h"
#include "transport/executor.h"
#include "transport/expected.h"

#include <cassert>
#include <functional>
#include <memory>
#include <span>
#include <string>

namespace transport {

class any_transport;

class Connector {
 public:
  virtual ~Connector() = default;

  [[nodiscard]] virtual awaitable<error_code> open() = 0;
};

class Sender {
 public:
  virtual ~Sender() = default;

  // Caller must retain the buffer until the operation completes.
  // Returns amount of bytes written or an error.
  [[nodiscard]] virtual awaitable<expected<size_t>> write(
      std::span<const char> buffer) = 0;
};

class Reader {
 public:
  virtual ~Reader() = default;

  // For streaming transports. Caller must retain the buffer until the operation
  // completes. Returns zero when transport is closed.
  [[nodiscard]] virtual awaitable<expected<size_t>> read(
      std::span<char> buffer) = 0;
};

class TransportMetadata {
 public:
  virtual ~TransportMetadata() = default;

  [[nodiscard]] virtual std::string name() const = 0;

  // Transport supports messages by itself without using of MessageReader.
  // If returns false, Read has to be implemented.
  [[nodiscard]] virtual bool message_oriented() const = 0;

  [[nodiscard]] virtual bool connected() const = 0;

  // Active means the transport is a client (not a server) transport.
  [[nodiscard]] virtual bool active() const = 0;
};

// TODO: Make non-virtual.
// TODO: Split per a stream and a message-oriented transport.
// TODO: Split per a connected transport and a connector.
class Transport : public Connector,
                  public Reader,
                  public Sender,
                  public TransportMetadata {
 public:
  Transport() = default;
  virtual ~Transport() = default;

  Transport(const Transport&) = delete;
  Transport& operator=(const Transport&) = delete;

  [[nodiscard]] virtual executor get_executor() = 0;

  [[nodiscard]] virtual awaitable<expected<any_transport>> accept() = 0;

  [[nodiscard]] virtual awaitable<error_code> close() = 0;
};

}  // namespace transport