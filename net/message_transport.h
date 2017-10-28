#pragma once

#include <memory>

#include "base/memory/ref_counted.h"
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
  virtual Error Open() override;
  virtual void Close() override;
  virtual int Read(void* data, size_t len) override;
  virtual int Write(const void* data, size_t len) override;
  virtual std::string GetName() const override;
  virtual bool IsMessageOriented() const override;
  virtual bool IsConnected() const override { return child_transport_->IsConnected(); }
  virtual bool IsActive() const { return child_transport_->IsActive(); }

 private:
  class Context : public base::RefCountedThreadSafe<Context> {
   public:
    Context() : destroyed_(false) {}

    bool is_destroyed() const { return destroyed_; }

    void Destroy() { destroyed_ = true; }

   private:
    bool destroyed_;
  };

  int InternalRead(void* data, size_t len);

  void InternalClose();

  // Transport::Delegate
  virtual void OnTransportOpened() override;
  virtual net::Error OnTransportAccepted(std::unique_ptr<Transport> transport) override;
  virtual void OnTransportClosed(Error error) override;
  virtual void OnTransportDataReceived() override;

  std::unique_ptr<Transport> child_transport_;

  std::unique_ptr<MessageReader> message_reader_;

  size_t max_message_size_;
  // TODO: Move into Context.
  std::vector<char> read_buffer_;

  scoped_refptr<Context> context_;
};

} // namespace net
