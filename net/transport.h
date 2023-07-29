#pragma once

#include "net/base/net_errors.h"
#include "net/base/net_export.h"
#include "net/promise.h"

#include <cassert>
#include <functional>
#include <memory>
#include <span>
#include <string>

namespace net {

class NET_EXPORT Transport {
 public:
  using OpenHandler = std::function<void()>;
  using CloseHandler = std::function<void(Error error)>;
  using DataHandler = std::function<void()>;
  using MessageHandler = std::function<void(std::span<const char>)>;
  using AcceptHandler = std::function<void(std::unique_ptr<Transport>)>;

  struct Handlers {
    OpenHandler on_open;
    // Triggered also when open fails.
    CloseHandler on_close;
    // For streaming transports.
    DataHandler on_data;
    MessageHandler on_message;
    AcceptHandler on_accept;
  };

  Transport() = default;
  virtual ~Transport() = default;

  Transport(const Transport&) = delete;
  Transport& operator=(const Transport&) = delete;

  virtual void Open(const Handlers& handlers) = 0;

  virtual void Close() = 0;

  // For streaming transports. After reception of `on_data` event, the client
  // can read received data gradually.
  // Returns a negative |Error| on failure.
  virtual int Read(std::span<char> data) = 0;

  // Returns amount of bytes written or an error.
  virtual promise<size_t> Write(std::span<const char> data) = 0;

  virtual std::string GetName() const = 0;

  // Transport supports messages by itself without using of MessageReader.
  // If returns false, Read has to be implemented.
  virtual bool IsMessageOriented() const = 0;

  virtual bool IsConnected() const = 0;

  // Active means the transport is a client (not a server) transport.
  virtual bool IsActive() const = 0;
};

}  // namespace net