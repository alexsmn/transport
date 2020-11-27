#pragma once

#include <memory>

#include "net/base/net_export.h"
#include "net/transport.h"

namespace net {

class MessageReader;

class NET_EXPORT MessageTransport : public Transport,
                                    private Transport::Delegate {
 public:
  MessageTransport(std::unique_ptr<Transport> child_transport,
                   std::unique_ptr<MessageReader> message_reader);
  virtual ~MessageTransport();

  MessageReader& message_reader() { return *message_reader_; }

  // Transport
  virtual Error Open(Transport::Delegate& delegate) override;
  virtual void Close() override;
  virtual int Read(void* data, size_t len) override;
  virtual int Write(const void* data, size_t len) override;
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

  // Transport::Delegate
  virtual void OnTransportOpened() override;
  virtual net::Error OnTransportAccepted(
      std::unique_ptr<Transport> transport) override;
  virtual void OnTransportClosed(Error error) override;
  virtual void OnTransportDataReceived() override;
  virtual void OnTransportMessageReceived(const void* data,
                                          size_t size) override;

  std::unique_ptr<Transport> child_transport_;

  std::unique_ptr<MessageReader> message_reader_;

  Transport::Delegate* delegate_ = nullptr;

  size_t max_message_size_;
  // TODO: Move into Context.
  std::vector<char> read_buffer_;

  std::shared_ptr<bool> cancelation_;
};

}  // namespace net
