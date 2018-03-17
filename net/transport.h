#pragma once

#include "net/base/net_errors.h"
#include "net/base/net_export.h"

#include <cassert>
#include <memory>
#include <string>

namespace net {

class NET_EXPORT Transport {
 public:
  struct Delegate {
    virtual void OnTransportOpened() {}
    virtual void OnTransportClosed(Error error) {}
    // Transport-level data was received.
    virtual void OnTransportDataReceived() {}
    virtual void OnTransportMessageReceived(const void* data, size_t size) {}
    virtual net::Error OnTransportAccepted(std::unique_ptr<Transport> transport) {
      return net::ERR_ACCESS_DENIED;
    }
  };

  Transport() {}
  virtual ~Transport() {}

  Transport(const Transport&) = delete;
  Transport& operator=(const Transport&) = delete;

  // Returns |net::Error| on failure.
  virtual Error Open(Delegate& delegate) = 0;

  virtual void Close() = 0;

  // Returns |net::Error| on failure.
  virtual int Read(void* data, size_t len) = 0;

  // Returns |net::Error| on failure.
  virtual int Write(const void* data, size_t len) = 0;

  virtual std::string GetName() const = 0;

  // Transport supports messages by itself without using of MessageReader.
  // If returns false, Read has to be implemented.
  virtual bool IsMessageOriented() const = 0;

  virtual bool IsConnected() const = 0;

  virtual bool IsActive() const = 0;
};

} // namespace net
