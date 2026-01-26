#include "net/websocket_transport.h"

#include "net/transport_delegate_mock.h"

#include <boost/asio/io_context.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <gmock/gmock.h>
#include <random>
#include <thread>
#include <unordered_set>

using namespace testing;

namespace net {

namespace {

inline int GenerateTestNetworkPort() {
  static std::mt19937 gen(std::random_device{}());
  static std::uniform_int_distribution distrib{30000, 40000};
  static std::unordered_set<int> seen;
  int port = distrib(gen);
  while (!seen.emplace(port).second) {
    port = distrib(gen);
  }
  return port;
}

// Helper to run io_context with timeout
void RunWithTimeout(boost::asio::io_context& io_context,
                    std::chrono::milliseconds timeout) {
  io_context.run_for(timeout);
}

}  // namespace

class WebSocketTransportTest : public Test {
 protected:
  void SetUp() override { port_ = GenerateTestNetworkPort(); }

  int port_ = 0;
};

TEST_F(WebSocketTransportTest, ServerOpen_CallsOnOpen) {
  boost::asio::io_context io_context;

  WebSocketTransport server{io_context, "127.0.0.1", port_};

  bool on_open_called = false;

  server.Open({.on_open = [&] {
    on_open_called = true;
    io_context.stop();
  }});

  RunWithTimeout(io_context, std::chrono::seconds(2));

  EXPECT_TRUE(on_open_called);
}

TEST_F(WebSocketTransportTest, ClientConnects_ServerCallsOnAccept) {
  boost::asio::io_context io_context;

  WebSocketTransport server{io_context, "127.0.0.1", port_};

  bool on_accept_called = false;
  std::unique_ptr<Transport> accepted_transport;

  server.Open({
      .on_accept =
          [&](std::unique_ptr<Transport> transport) {
            on_accept_called = true;
            accepted_transport = std::move(transport);
            io_context.stop();
          },
  });

  // Connect a WebSocket client
  boost::beast::websocket::stream<boost::beast::tcp_stream> ws{io_context};

  ws.next_layer().async_connect(
      boost::asio::ip::tcp::endpoint{boost::asio::ip::make_address("127.0.0.1"),
                                     static_cast<unsigned short>(port_)},
      [&](boost::beast::error_code ec) {
        ASSERT_FALSE(ec) << ec.message();
        ws.async_handshake("127.0.0.1", "/",
                           [&](boost::beast::error_code ec) {
                             ASSERT_FALSE(ec) << ec.message();
                           });
      });

  RunWithTimeout(io_context, std::chrono::seconds(2));

  EXPECT_TRUE(on_accept_called);
  EXPECT_NE(accepted_transport, nullptr);
}

TEST_F(WebSocketTransportTest, ClientSendsMessage_ServerReceives) {
  boost::asio::io_context io_context;

  WebSocketTransport server{io_context, "127.0.0.1", port_};

  std::string received_message;
  std::unique_ptr<Transport> accepted_transport;

  server.Open({
      .on_accept =
          [&](std::unique_ptr<Transport> transport) {
            accepted_transport = std::move(transport);
            accepted_transport->Open({
                .on_message =
                    [&](std::span<const char> data) {
                      received_message = std::string(data.data(), data.size());
                      io_context.stop();
                    },
            });
          },
  });

  // Connect a WebSocket client and send a message
  boost::beast::websocket::stream<boost::beast::tcp_stream> ws{io_context};
  ws.binary(true);

  ws.next_layer().async_connect(
      boost::asio::ip::tcp::endpoint{boost::asio::ip::make_address("127.0.0.1"),
                                     static_cast<unsigned short>(port_)},
      [&](boost::beast::error_code ec) {
        ASSERT_FALSE(ec) << ec.message();
        ws.async_handshake(
            "127.0.0.1", "/", [&](boost::beast::error_code ec) {
              ASSERT_FALSE(ec) << ec.message();
              ws.async_write(boost::asio::buffer(std::string("Hello WebSocket")),
                             [&](boost::beast::error_code ec, std::size_t) {
                               ASSERT_FALSE(ec) << ec.message();
                             });
            });
      });

  RunWithTimeout(io_context, std::chrono::seconds(2));

  EXPECT_EQ(received_message, "Hello WebSocket");
}

TEST_F(WebSocketTransportTest, ServerSendsMessage_ClientReceives) {
  boost::asio::io_context io_context;

  WebSocketTransport server{io_context, "127.0.0.1", port_};

  std::string received_message;
  std::unique_ptr<Transport> accepted_transport;

  server.Open({
      .on_accept =
          [&](std::unique_ptr<Transport> transport) {
            accepted_transport = std::move(transport);
            accepted_transport->Open({});
            accepted_transport->Write(std::span<const char>{"Hello Client", 12});
          },
  });

  // Connect a WebSocket client and receive a message
  boost::beast::websocket::stream<boost::beast::tcp_stream> ws{io_context};
  ws.binary(true);
  boost::beast::flat_buffer buffer;

  ws.next_layer().async_connect(
      boost::asio::ip::tcp::endpoint{boost::asio::ip::make_address("127.0.0.1"),
                                     static_cast<unsigned short>(port_)},
      [&](boost::beast::error_code ec) {
        ASSERT_FALSE(ec) << ec.message();
        ws.async_handshake(
            "127.0.0.1", "/", [&](boost::beast::error_code ec) {
              ASSERT_FALSE(ec) << ec.message();
              ws.async_read(buffer, [&](boost::beast::error_code ec,
                                        std::size_t bytes_transferred) {
                ASSERT_FALSE(ec) << ec.message();
                received_message = std::string(
                    static_cast<const char*>(buffer.data().data()),
                    bytes_transferred);
                io_context.stop();
              });
            });
      });

  RunWithTimeout(io_context, std::chrono::seconds(2));

  EXPECT_EQ(received_message, "Hello Client");
}

TEST_F(WebSocketTransportTest, ClientDisconnects_ServerCallsOnClose) {
  boost::asio::io_context io_context;

  WebSocketTransport server{io_context, "127.0.0.1", port_};

  bool on_close_called = false;
  Error close_error = ERR_FAILED;
  std::unique_ptr<Transport> accepted_transport;

  server.Open({
      .on_accept =
          [&](std::unique_ptr<Transport> transport) {
            accepted_transport = std::move(transport);
            accepted_transport->Open({
                .on_close =
                    [&](Error error) {
                      on_close_called = true;
                      close_error = error;
                      io_context.stop();
                    },
            });
          },
  });

  // Connect and then close
  auto ws = std::make_shared<
      boost::beast::websocket::stream<boost::beast::tcp_stream>>(io_context);

  ws->next_layer().async_connect(
      boost::asio::ip::tcp::endpoint{boost::asio::ip::make_address("127.0.0.1"),
                                     static_cast<unsigned short>(port_)},
      [&, ws](boost::beast::error_code ec) {
        ASSERT_FALSE(ec) << ec.message();
        ws->async_handshake(
            "127.0.0.1", "/", [&, ws](boost::beast::error_code ec) {
              ASSERT_FALSE(ec) << ec.message();
              // Close the connection gracefully
              ws->async_close(boost::beast::websocket::close_code::normal,
                              [](boost::beast::error_code) {});
            });
      });

  RunWithTimeout(io_context, std::chrono::seconds(2));

  EXPECT_TRUE(on_close_called);
  EXPECT_EQ(close_error, OK);
}

TEST_F(WebSocketTransportTest, ServerClose_StopsAccepting) {
  boost::asio::io_context io_context;

  WebSocketTransport server{io_context, "127.0.0.1", port_};

  bool on_open_called = false;

  server.Open({.on_open = [&] { on_open_called = true; }});

  // Run briefly to let server start
  RunWithTimeout(io_context, std::chrono::milliseconds(100));

  EXPECT_TRUE(on_open_called);

  // Close the server
  server.Close();

  // Try to connect - the connection or handshake should fail or timeout.
  // If we successfully complete a WebSocket handshake, that's a test failure.
  boost::beast::websocket::stream<boost::beast::tcp_stream> ws{io_context};
  bool handshake_succeeded = false;

  ws.next_layer().async_connect(
      boost::asio::ip::tcp::endpoint{boost::asio::ip::make_address("127.0.0.1"),
                                     static_cast<unsigned short>(port_)},
      [&](boost::beast::error_code ec) {
        if (ec) {
          // Connect failed - server is properly closed
          io_context.stop();
          return;
        }
        // TCP connected, try WebSocket handshake
        ws.async_handshake("127.0.0.1", "/", [&](boost::beast::error_code ec) {
          handshake_succeeded = !ec;
          io_context.stop();
        });
      });

  RunWithTimeout(io_context, std::chrono::seconds(2));

  // The test passes if the handshake did NOT succeed (either failed or timed out)
  EXPECT_FALSE(handshake_succeeded);
}

}  // namespace net
