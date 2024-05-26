#pragma once

#include "net/awaitable.h"
#include "net/base/net_errors.h"
#include "net/base/net_export.h"
#include "net/error_or.h"
#include "net/executor.h"

#include <cassert>
#include <functional>
#include <memory>
#include <span>
#include <string>

namespace net {

class Transport;

class Connector {
 public:
  virtual ~Connector() = default;

  using OpenHandler = std::function<void()>;
  using CloseHandler = std::function<void(Error error)>;
  using AcceptHandler = std::function<void(std::unique_ptr<Transport>)>;

  struct Handlers {
    // Triggered also when open fails.
    CloseHandler on_close;
    // TODO: Introduce an `Accept` method returning a promised transport.
    AcceptHandler on_accept;
  };

  [[nodiscard]] virtual awaitable<Error> Open(Handlers handlers = {}) = 0;
};

class Sender {
 public:
  virtual ~Sender() = default;

  // Caller must retain the buffer until the operation completes.
  // Returns amount of bytes written or an error.
  [[nodiscard]] virtual awaitable<ErrorOr<size_t>> Write(
      std::span<const char> buffer) = 0;
};

class Reader {
 public:
  virtual ~Reader() = default;

  // For streaming transports. Caller must retain the buffer until the operation
  // completes. Returns zero when transport is closed.
  [[nodiscard]] virtual awaitable<ErrorOr<size_t>> Read(
      std::span<char> buffer) = 0;
};

class TransportMetadata {
 public:
  virtual ~TransportMetadata() = default;

  [[nodiscard]] virtual std::string GetName() const = 0;

  // Transport supports messages by itself without using of MessageReader.
  // If returns false, Read has to be implemented.
  [[nodiscard]] virtual bool IsMessageOriented() const = 0;

  [[nodiscard]] virtual bool IsConnected() const = 0;

  // Active means the transport is a client (not a server) transport.
  [[nodiscard]] virtual bool IsActive() const = 0;
};

// TODO: Make non-virtual.
// TODO: Split per a stream and a message-oriented transport.
// TODO: Split per a connected transport and a connector.
class NET_EXPORT Transport : public Connector,
                             public Reader,
                             public Sender,
                             public TransportMetadata {
 public:
  Transport() = default;
  virtual ~Transport() = default;

  Transport(const Transport&) = delete;
  Transport& operator=(const Transport&) = delete;

  [[nodiscard]] virtual Executor GetExecutor() const = 0;

  // TODO: Should be a coroutine.
  virtual void Close() = 0;
};

}  // namespace net