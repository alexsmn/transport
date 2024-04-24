#include "net/transport.h"
#include "net/test/debug_logger.h"
#include "net/transport_factory_impl.h"
#include "net/transport_string.h"

#include <array>
#include <boost/asio/bind_executor.hpp>
#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/strand.hpp>
#include <gmock/gmock.h>
#include <ranges>

using namespace testing;

namespace net {

// The test parameter is a transport string with no active/passive parameter.
class TransportTest : public TestWithParam<std::string /*transport_string*/> {
 public:
  TransportTest();

 protected:
  virtual void SetUp() override;
  virtual void TearDown() override;

  boost::asio::io_context io_context_;

  TransportFactoryImpl transport_factory_{io_context_};

  struct Server {
    promise<void> Init();

    Executor executor_;
    TransportFactory& transport_factory_;

    std::shared_ptr<Logger> logger_ =
        std::make_shared<ProxyLogger>(kLogger, "Server");

    std::unique_ptr<Transport> transport_ = transport_factory_.CreateTransport(
        TransportString{GetParam() + ";Passive"},
        executor_,
        logger_);
  };

  struct Client {
    promise<void> Run();

    std::string name_;
    Executor executor_;
    TransportFactory& transport_factory_;

    std::shared_ptr<Logger> logger_ =
        std::make_shared<ProxyLogger>(kLogger, name_.c_str());

    std::unique_ptr<Transport> transport_ = transport_factory_.CreateTransport(
        TransportString{GetParam() + ";Active"},
        executor_,
        logger_);
  };

  std::vector<std::jthread> threads_;

  Server server_{boost::asio::make_strand(io_context_), transport_factory_};
  std::vector<std::unique_ptr<Client>> clients_;

  // Must be the latest member.
  boost::asio::executor_work_guard<boost::asio::io_context::executor_type>
      io_context_work_guard_ = boost::asio::make_work_guard(io_context_);

  static inline std::shared_ptr<Logger> kLogger =
      std::make_shared<DebugLogger>();

  static const int kThreadCount = 10;
  static const int kClientCount = 100;
  static constexpr const char kMessage[] = "Message";
};

INSTANTIATE_TEST_SUITE_P(AllTransportTests,
                         TransportTest,
                         // TODO: Random port.
                         // TODO: Implement send retries and enable UDP tests.
                         testing::Values("TCP;Port=4321" /*,
                                         "UDP;Port=4322"*/));

// TransportTest::Server

promise<void> TransportTest::Server::Init() {
  return transport_->Open(
      {.on_accept = [this](std::unique_ptr<Transport> accepted_transport) {
        logger_->Write(LogSeverity::Normal, "Connected");
        auto tr = std::shared_ptr<Transport>{accepted_transport.release()};
        tr->Open({.on_close =
                      [this, tr](net::Error) mutable {
                        logger_->Write(LogSeverity::Warning, "Disconnected");
                        tr.reset();
                      },
                  .on_data =
                      [this, &tr = *tr] {
                        logger_->Write(LogSeverity::Normal,
                                       "Data received. Send echo");
                        for (;;) {
                          std::array<char, 64> buffer;
                          int res = tr.Read(buffer);
                          if (res <= 0) {
                            break;
                          }
                          tr.Write(std::span{buffer}.subspan(0, res));
                        }
                      },
                  .on_message =
                      [this, &tr = *tr](std::span<const char> data) {
                        logger_->Write(LogSeverity::Normal,
                                       "Message received. Send echo");
                        tr.Write(data);
                      }});
      }});
}

// TransportTest::Client

promise<void> TransportTest::Client::Run() {
  promise<void> received_message;
  transport_
      ->Open({.on_close =
                  [this, received_message](Error error) mutable {
                    logger_->Write(LogSeverity::Normal, "Closed");
                    received_message.reject(net_exception{error});
                  },
              .on_data =
                  [this, received_message]() mutable {
                    logger_->Write(LogSeverity::Normal, "Message received");
                    std::array<char, std::size(kMessage)> data;
                    if (transport_->Read(data) != std::size(kMessage)) {
                      throw std::runtime_error{"Received message is too short"};
                    }
                    if (!std::ranges::equal(data, kMessage)) {
                      throw std::runtime_error{
                          "Received message is not equal to the sent one."};
                    }
                    received_message.resolve();
                  },
              .on_message =
                  [this, received_message](std::span<const char> data) mutable {
                    logger_->Write(LogSeverity::Normal,
                                   "Message received. Send echo");
                    transport_->Write(data);
                    if (!std::ranges::equal(data, kMessage)) {
                      throw std::runtime_error{
                          "Received message is not equal to the sent one."};
                    }
                    received_message.resolve();
                  }})
      .then([this] {
        logger_->Write(LogSeverity::Normal, "Send message");
        transport_->Write(kMessage);
      });
  return received_message.then([this] { transport_->Close(); });
}

// TransportTest

TransportTest::TransportTest() {}

void TransportTest::SetUp() {
  for (int i = 0; i < kClientCount; ++i) {
    clients_.emplace_back(std::make_unique<Client>(
        /*name=*/std::format("Client {}", i + 1),
        /*executor=*/boost::asio::make_strand(io_context_),
        transport_factory_));
  }

  for (int i = 0; i < kThreadCount; ++i) {
    threads_.emplace_back([&io_context = io_context_] { io_context.run(); });
  }
}

void TransportTest::TearDown() {}

TEST_P(TransportTest, Test) {
  // Connect all.

  server_.Init().get();

  net::make_all_promise(clients_ | std::views::transform(&Client::Run)).get();
}

}  // namespace net
