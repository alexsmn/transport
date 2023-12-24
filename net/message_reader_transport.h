#pragma once

#include "net/base/net_export.h"
#include "net/transport.h"

#include <base/threading/thread_collision_warner.h>
#include <memory>

namespace net {

class Logger;
class MessageReader;

// A message-oriented transport constructed from a child transport using a
// provided message reader.
//
// Supports both message and stream child transports. When message child
// transport is used, the message reader is applied on concatenated child
// transport messages.
class NET_EXPORT MessageReaderTransport : public Transport {
 public:
  MessageReaderTransport(std::unique_ptr<Transport> child_transport,
                         std::unique_ptr<MessageReader> message_reader,
                         std::shared_ptr<const Logger> logger);
  virtual ~MessageReaderTransport();

  MessageReader& message_reader() { return *message_reader_; }

  // Transport
  virtual void Open(const Handlers& handlers) override;
  virtual void Close() override;
  virtual int Read(std::span<char> data) override;
  virtual promise<size_t> Write(std::span<const char> data) override;
  virtual std::string GetName() const override;
  virtual bool IsMessageOriented() const override;
  virtual bool IsConnected() const override {
    return child_transport_->IsConnected();
  }
  virtual bool IsActive() const override {
    return child_transport_->IsActive();
  }

 private:
  int InternalRead(void* data, size_t len);

  void InternalClose();

  // Child handlers.
  void OnChildTransportOpened();
  void OnChildTransportAccepted(std::unique_ptr<Transport> transport);
  void OnChildTransportClosed(Error error);
  void OnChildTransportDataReceived();
  void OnChildTransportMessageReceived(std::span<const char> data);

  std::unique_ptr<Transport> child_transport_;

  std::unique_ptr<MessageReader> message_reader_;

  const std::shared_ptr<const Logger> logger_;

  Handlers handlers_;

  // TODO: Move into Context.
  std::vector<char> read_buffer_;

  std::shared_ptr<bool> cancelation_;

  DFAKE_MUTEX(mutex_);
};

}  // namespace net
