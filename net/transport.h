#pragma once

#include "net/awaitable.h"
#include "net/base/net_errors.h"
#include "net/base/net_export.h"
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
  using DataHandler = std::function<void()>;
  using MessageHandler = std::function<void(std::span<const char>)>;
  using AcceptHandler = std::function<void(std::unique_ptr<Transport>)>;

  struct Handlers {
    // TODO: Remove `on_open` and keep only `on_accept`.
    OpenHandler on_open;
    // Triggered also when open fails.
    CloseHandler on_close;
    // For streaming transports.
    // TODO: Remove and substitute with a promised `Read`.
    DataHandler on_data;
    // For message-oriented transports.
    // TODO: Remove and substitute with a promised `Read`.
    MessageHandler on_message;
    // TODO: Introduce an `Accept` method returning a promised transport.
    AcceptHandler on_accept;
  };

  [[nodiscard]] virtual awaitable<Error> Open(Handlers handlers) = 0;
};

class Sender {
 public:
  virtual ~Sender() = default;

  // Caller must retain the buffer until the operation completes.
  // Returns amount of bytes written or an error.
  [[nodiscard]] virtual awaitable<ErrorOr<size_t>> Write(
      std::span<const char> data) = 0;
};

class Reader {
 public:
  virtual ~Reader() = default;

  // For streaming transports. After reception of `on_data` event, the client
  // can read received data gradually.
  // Returns a negative |Error| on failure.
  // TODO: This should return a promised buffer.
  virtual int Read(std::span<char> data) = 0;
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

  // TODO: Should return a promise.
  virtual void Close() = 0;
};

}  // namespace net