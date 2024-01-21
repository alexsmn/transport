#include "net/transport.h"
#include "net/transport_factory_impl.h"
#include "net/transport_string.h"

#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/thread_pool.hpp>
#include <gmock/gmock.h>
#include <ranges>

using namespace testing;

namespace net {

class TransportTest : public Test {
 public:
  TransportTest();

 protected:
  virtual void SetUp() override;
  virtual void TearDown() override;

  boost::asio::thread_pool thread_pool_{kThreadCount};

  boost::asio::io_context io_context_;
  boost::asio::executor_work_guard<boost::asio::io_context::executor_type>
      io_context_work_guard_ = boost::asio::make_work_guard(io_context_);

  TransportFactoryImpl transport_factory_{io_context_};

  struct Server {
    promise<void> Init();

    TransportFactory& transport_factory_;

    std::unique_ptr<Transport> transport_ = transport_factory_.CreateTransport(
        TransportString{"TCP;Passive;Port=4321"});
  };

  struct Client {
    promise<void> Init();

    TransportFactory& transport_factory_;

    std::unique_ptr<Transport> transport_ = transport_factory_.CreateTransport(
        TransportString{"TCP;Active;Port=4321"});
  };

  Server server_{transport_factory_};
  std::vector<std::unique_ptr<Client>> clients_;

  static const int kThreadCount = 10;
  static const int kClientCount = 100;
};

// TransportTest::Server

promise<void> TransportTest::Server::Init() {
  return transport_->Open(Transport::Handlers{});
}

// TransportTest::Client

promise<void> TransportTest::Client::Init() {
  return transport_->Open(Transport::Handlers{});
}

// TransportTest

TransportTest::TransportTest() {}

void TransportTest::SetUp() {
  for (int i = 0; i < kClientCount; ++i) {
    auto client = std::make_unique<Client>(transport_factory_);
    clients_.emplace_back(std::move(client));
  }

  for (int i = 0; i < kThreadCount; ++i) {
    boost::asio::post(thread_pool_,
                      // Cannot use `std::bind_front` since `io_context::run`
                      // returns a non-void value.
                      [&io_context = io_context_] { io_context.run(); });
  }
}

void TransportTest::TearDown() {}

TEST_F(TransportTest, Test) {
  // Connect all.

  server_.Init().get();

  net::make_all_promise(clients_ | std::views::transform(&Client::Init)).get();
}

}  // namespace net
