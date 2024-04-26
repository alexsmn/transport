#include "net/websocket_transport.h"

#include "net/transport_util.h"

#include <boost/asio/as_tuple.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <queue>

namespace net {

class WebSocketTransport::ConnectionCore
    : public std::enable_shared_from_this<ConnectionCore> {
 public:
  ConnectionCore(boost::beast::websocket::stream<boost::beast::tcp_stream> ws)
      : ws{std::move(ws)} {}

  [[nodiscard]] boost::asio::awaitable<void> Open(Handlers handlers);
  void Close();

  [[nodiscard]] boost::asio::awaitable<size_t> Write(std::vector<char> data);

 private:
  [[nodiscard]] boost::asio::awaitable<void> StartReading();
  [[nodiscard]] boost::asio::awaitable<void> StartWriting();

  boost::beast::websocket::stream<boost::beast::tcp_stream> ws;

  Handlers handlers_;

  std::queue<std::vector<char>> write_queue_;
  bool writing_ = false;
};

boost::asio::awaitable<void> WebSocketTransport::ConnectionCore::Open(
    Handlers handlers) {
  handlers_ = handlers;

  boost::asio::co_spawn(ws.get_executor(), StartReading(),
                        boost::asio::detached);

  co_return;
}

boost::asio::awaitable<void>
WebSocketTransport::ConnectionCore::StartReading() {
  auto ref = shared_from_this();

  for (;;) {
    boost::beast::flat_buffer buffer;

    // Read a message
    auto [ec, _] = co_await ws.async_read(
        buffer, boost::asio::as_tuple(boost::asio::use_awaitable));

    // This indicates that the session was closed
    if (ec == boost::beast::websocket::error::closed)
      break;

    if (ec) {
      if (handlers_.on_close)
        handlers_.on_close(net::ERR_ABORTED);
      co_return;
    }

    if (handlers_.on_message) {
      handlers_.on_message({static_cast<const char*>(buffer.data().data()),
                            buffer.data().size()});
    }
  }

  if (handlers_.on_close) {
    handlers_.on_close(net::OK);
  }
}

void WebSocketTransport::ConnectionCore::Close() {
  handlers_ = {};

  boost::beast::error_code ec;
  ws.close(boost::beast::websocket::close_code::normal, ec);
}

boost::asio::awaitable<size_t> WebSocketTransport::ConnectionCore::Write(
    std::vector<char> data) {
  auto data_size = data.size();
  write_queue_.emplace(std::move(data));

  if (!writing_) {
    writing_ = true;
    boost::asio::co_spawn(ws.get_executor(), StartWriting(),
                          boost::asio::detached);
  }

  // TODO: Proper async.
  co_return data_size;
}

boost::asio::awaitable<void>
WebSocketTransport::ConnectionCore::StartWriting() {
  while (!write_queue_.empty()) {
    auto message = std::move(write_queue_.front());
    write_queue_.pop();

    auto [ec, _] = co_await ws.async_write(
        boost::asio::buffer(message),
        boost::asio::as_tuple(boost::asio::use_awaitable));

    if (ec) {
      if (handlers_.on_close) {
        handlers_.on_close(net::ERR_ABORTED);
      }
      co_return;
    }
  }
}

// WebSocketTransport::Connection

class WebSocketTransport::Connection : public Transport {
 public:
  explicit Connection(std::shared_ptr<ConnectionCore> core)
      : core_{std::move(core)} {}
  ~Connection();

  // Transport
  [[nodiscard]] virtual boost::asio::awaitable<void> Open(
      Handlers handlers) override;

  virtual void Close() override;
  virtual int Read(std::span<char> data) override { return OK; }

  [[nodiscard]] virtual boost::asio::awaitable<size_t> Write(
      std::vector<char> data) override;

  virtual std::string GetName() const override { return "WebSocket"; }
  virtual bool IsMessageOriented() const override { return true; }
  virtual bool IsConnected() const override { return true; }
  virtual bool IsActive() const override { return false; }

 private:
  const std::shared_ptr<ConnectionCore> core_;
  bool opened_ = false;
};

WebSocketTransport::Connection::~Connection() {
  if (opened_)
    core_->Close();
}

boost::asio::awaitable<void> WebSocketTransport::Connection::Open(
    Handlers handlers) {
  assert(!opened_);

  opened_ = true;

  return core_->Open(std::move(handlers));
}

void WebSocketTransport::Connection::Close() {
  opened_ = false;
  core_->Close();
}

boost::asio::awaitable<size_t> WebSocketTransport::Connection::Write(
    std::vector<char> data) {
  assert(opened_);
  return core_->Write(std::move(data));
}

// WebSocketTransport::Core

class WebSocketTransport::Core : public std::enable_shared_from_this<Core> {
 public:
  Core(boost::asio::io_context& io_context, std::string host, int port);

  [[nodiscard]] boost::asio::awaitable<void> Open(Handlers handlers);
  void Close();

  [[nodiscard]] boost::asio::awaitable<void> StartAccepting();

  [[nodiscard]] boost::asio::awaitable<void> DoSession(
      boost::beast::websocket::stream<boost::beast::tcp_stream> ws);

 private:
  boost::asio::io_context& io_context_;
  const std::string host_;
  const int port_;

  Handlers handlers_;

  boost::asio::ip::tcp::acceptor acceptor_{io_context_};
};

WebSocketTransport::Core::Core(boost::asio::io_context& io_context,
                               std::string host,
                               int port)
    : io_context_{io_context}, host_{std::move(host)}, port_{port} {}

boost::asio::awaitable<void> WebSocketTransport::Core::Open(Handlers handlers) {
  handlers_ = std::move(handlers);

  auto const address = boost::asio::ip::make_address(host_);
  auto const port = static_cast<unsigned short>(port_);
  const boost::asio::ip::tcp::endpoint endpoint{address, port};

  boost::beast::error_code ec;

  // Open the acceptor
  acceptor_.open(endpoint.protocol(), ec);
  if (ec) {
    co_return;
  }

  // Allow address reuse
  acceptor_.set_option(boost::asio::socket_base::reuse_address(true), ec);
  if (ec) {
    co_return;
  }

  // Bind to the server address
  acceptor_.bind(endpoint, ec);
  if (ec) {
    co_return;
  }

  // Start listening for connections
  acceptor_.listen(boost::asio::socket_base::max_listen_connections, ec);
  if (ec) {
    co_return;
  }

  if (auto on_open = std::move(handlers_.on_open)) {
    on_open();
  }

  boost::asio::co_spawn(io_context_, StartAccepting(), boost::asio::detached);
}

void WebSocketTransport::Core::Close() {
  handlers_ = {};
}

boost::asio::awaitable<void> WebSocketTransport::Core::StartAccepting() {
  for (;;) {
    boost::asio::ip::tcp::socket socket(io_context_);

    auto [ec] = co_await acceptor_.async_accept(
        socket, boost::asio::as_tuple(boost::asio::use_awaitable));

    if (ec) {
      co_return;
    }

    boost::asio::co_spawn(
        acceptor_.get_executor(),
        DoSession(boost::beast::websocket::stream<boost::beast::tcp_stream>{
            std::move(socket)}),
        boost::asio::detached);
  }
}

boost::asio::awaitable<void> WebSocketTransport::Core::DoSession(
    boost::beast::websocket::stream<boost::beast::tcp_stream> ws) {
  ws.binary(true);

  // Set suggested timeout settings for the websocket
  ws.set_option(boost::beast::websocket::stream_base::timeout::suggested(
      boost::beast::role_type::server));

  // Set a decorator to change the Server of the handshake
  ws.set_option(boost::beast::websocket::stream_base::decorator(
      [](boost::beast::websocket::response_type& res) {
        res.set(
            boost::beast::http::field::server,
            std::string(BOOST_BEAST_VERSION_STRING) + " websocket-server-coro");
      }));

  // Accept the websocket handshake
  auto [ec] = co_await ws.async_accept(
      boost::asio::as_tuple(boost::asio::use_awaitable));

  if (ec) {
    co_return;
  }

  if (handlers_.on_accept) {
    auto connection_core = std::make_shared<ConnectionCore>(std::move(ws));
    handlers_.on_accept(std::make_unique<Connection>(connection_core));
  }
}

// WebSocketTransport

WebSocketTransport::WebSocketTransport(boost::asio::io_context& io_context,
                                       std::string host,
                                       int port)
    : core_{std::make_shared<Core>(io_context, std::move(host), port)} {}

WebSocketTransport::~WebSocketTransport() {
  if (core_)
    core_->Close();
}

boost::asio::awaitable<void> WebSocketTransport::Open(Handlers handlers) {
  assert(core_);
  return core_->Open(std::move(handlers));
}

void WebSocketTransport::Close() {
  core_->Close();
  core_ = nullptr;
}

}  // namespace net
