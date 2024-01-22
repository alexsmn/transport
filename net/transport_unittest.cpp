#include "net/transport.h"
#include "net/test/debug_logger.h"
#include "net/transport_factory_impl.h"
#include "net/transport_string.h"

#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>
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

  boost::asio::io_context io_context_;

  TransportFactoryImpl transport_factory_{io_context_};

  struct Server {
    promise<void> Init();

    TransportFactory& transport_factory_;

    std::unique_ptr<Transport> transport_ = transport_factory_.CreateTransport(
        // TODO: Random port.
        TransportString{"TCP;Passive;Port=4321"},
        std::make_shared<ProxyLogger>(kLogger, "Server"));
  };

  struct Client {
    promise<void> Init();

    TransportFactory& transport_factory_;

    std::unique_ptr<Transport> transport_ = transport_factory_.CreateTransport(
        // TODO: Random port.
        TransportString{"TCP;Active;Port=4321"},
        // TODO: Client ID.
        std::make_shared<ProxyLogger>(kLogger, "Client"));
  };

  std::vector<std::jthread> threads_;

  Server server_{transport_factory_};
  std::vector<std::unique_ptr<Client>> clients_;

  // Must be the latest member.
  boost::asio::executor_work_guard<boost::asio::io_context::executor_type>
      io_context_work_guard_ = boost::asio::make_work_guard(io_context_);

  static inline std::shared_ptr<Logger> kLogger =
      std::make_shared<DebugLogger>();

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
    clients_.emplace_back(std::make_unique<Client>(transport_factory_));
  }

  for (int i = 0; i < kThreadCount; ++i) {
    threads_.emplace_back([&io_context = io_context_] { io_context.run(); });
  }
}

void TransportTest::TearDown() {}

TEST_F(TransportTest, Test) {
  // Connect all.

  server_.Init().get();

  net::make_all_promise(clients_ | std::views::transform(&Client::Init)).get();
}

}  // namespace net
