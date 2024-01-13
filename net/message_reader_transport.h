#pragma once

#include "net/transport.h"

#include <boost/asio/executor.hpp>
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
class MessageReaderTransport : public Transport {
 public:
  MessageReaderTransport(boost::asio::executor executor,
                         std::unique_ptr<Transport> child_transport,
                         std::unique_ptr<MessageReader> message_reader,
                         std::shared_ptr<const Logger> logger);
  virtual ~MessageReaderTransport();

  MessageReader& message_reader();

  // Transport
  virtual void Open(const Handlers& handlers) override;
  virtual void Close() override;
  virtual int Read(std::span<char> data) override;
  virtual promise<size_t> Write(std::span<const char> data) override;
  virtual std::string GetName() const override;
  virtual bool IsMessageOriented() const override;
  virtual bool IsConnected() const override;
  virtual bool IsActive() const override;

 private:
  struct Core;

  const std::shared_ptr<Core> core_;
};

}  // namespace net
