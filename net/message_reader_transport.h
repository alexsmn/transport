#pragma once

#include "net/base/net_export.h"
#include "net/message_reader.h"
#include "net/transport.h"

#include <base/threading/thread_collision_warner.h>
#include <boost/asio/io_context_strand.hpp>
#include <memory>

namespace net {

class Logger;

// A message-oriented transport constructed from a child transport using a
// provided message reader.
//
// Supports both message and stream child transports. When message child
// transport is used, the message reader is applied on concatenated child
// transport messages.
class NET_EXPORT MessageReaderTransport : public Transport {
 public:
  MessageReaderTransport(boost::asio::io_context& io_context,
                         std::unique_ptr<Transport> child_transport,
                         std::unique_ptr<MessageReader> message_reader,
                         std::shared_ptr<const Logger> logger);
  virtual ~MessageReaderTransport();

  MessageReader& message_reader() { return *core_->message_reader_; }

  // Transport
  virtual void Open(const Handlers& handlers) override;
  virtual void Close() override;
  virtual int Read(std::span<char> data) override;
  virtual promise<size_t> Write(std::span<const char> data) override;
  virtual std::string GetName() const override;
  virtual bool IsMessageOriented() const override;
  virtual bool IsConnected() const override {
    return core_->child_transport_->IsConnected();
  }
  virtual bool IsActive() const override {
    return core_->child_transport_->IsActive();
  }

 private:
  struct Core : std::enable_shared_from_this<Core> {
    Core(boost::asio::io_context& io_context,
         std::unique_ptr<Transport> child_transport,
         std::unique_ptr<MessageReader> message_reader,
         std::shared_ptr<const Logger> logger)
        : strand_{io_context},
          child_transport_{std::move(child_transport)},
          message_reader_{std::move(message_reader)},
          logger_{std::move(logger)} {}

    void Open(const Handlers& handlers);
    void Close();

    int ReadMessage(void* data, size_t len);
    promise<size_t> WriteMessage(std::span<const char> data);

    // Child handlers.
    void OnChildTransportOpened();
    void OnChildTransportAccepted(std::unique_ptr<Transport> transport);
    void OnChildTransportClosed(Error error);
    void OnChildTransportDataReceived();
    void OnChildTransportMessageReceived(std::span<const char> data);

    boost::asio::io_context::strand strand_;
    const std::unique_ptr<Transport> child_transport_;
    const std::unique_ptr<MessageReader> message_reader_;
    const std::shared_ptr<const Logger> logger_;

    Handlers handlers_;

    DFAKE_MUTEX(mutex_);

    // TODO: Move into Context.
    std::vector<char> read_buffer_ =
        std::vector<char>(message_reader_->message().capacity);

    // TODO: Remove and replace with `weak_from_this`.
    std::shared_ptr<bool> cancelation_;
  };

  const std::shared_ptr<Core> core_;
};

}  // namespace net
