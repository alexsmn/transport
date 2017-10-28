#include "base/at_exit.h"
#include "base/message_loop.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "net/queue_transport.h"
#include "net/session.h"

class TestTransportDelegate : public net::Transport::Delegate {
 public:
  TestTransportDelegate() : opened_(false) {}

  virtual void OnTransportOpened() override { opened_ = true; }
  virtual void OnTransportClosed(net::Error error) override { opened_ = false; }
  virtual void OnTransportDataReceived() override {}
  virtual void OnTransportMessageReceived(const void* data, size_t size) override {
    const char* chars = static_cast<const char*>(data);
    queue_.push(Message(chars, chars + size));
  }
  virtual net::Error OnTransportAccepted(
      std::unique_ptr<net::Transport>& transport) override {}

  bool opened_;

  typedef std::vector<char> Message;
  typedef std::queue<Message> MessageQueue;
  MessageQueue queue_;
};

class SessionTest : public testing::Test {
 protected:
  base::AtExitManager at_exit_;
  MessageLoop message_loop_;
};

TEST_F(SessionTest, Test) {
  TestTransportDelegate listener_delegate;
  net::Session listener;
  listener.set_delegate(&listener_delegate);
  net::QueueTransport* listener_transport = new net::QueueTransport;
  listener.SetTransport(listener_transport);

  // EXPECT_CALL(listener_delegate, OnTransportOpened());
  ASSERT_EQ(net::OK, listener.Open());
//  EXPECT_TRUE(listener_delegate.opened_);

  TestTransportDelegate client_delegate;
  net::Session client;
  client.set_delegate(&client_delegate);
  net::QueueTransport* client_transport = new net::QueueTransport;
  client_transport->SetActive(*listener_transport);
  client.SetTransport(client_transport);

  // EXPECT_CALL(client_delegate, OnTransportOpened());
  ASSERT_EQ(net::OK, client.Open());
  EXPECT_TRUE(client_delegate.opened_);

  message_loop_.Run();
}
