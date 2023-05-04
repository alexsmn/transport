#include "net/websocket_transport.h"

#include <boost/asio/spawn.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <queue>

namespace net {

class WebSocketTransport::ConnectionCore
    : public std::enable_shared_from_this<ConnectionCore> {
 public:
  ConnectionCore(boost::beast::websocket::stream<boost::beast::tcp_stream> ws,
                 boost::asio::yield_context yield)
      : ws{std::move(ws)}, yield{std::move(yield)} {}

  void Open(const Handlers& handlers);
  void Close();

  int Write(const void* data, size_t len);

 private:
  void StartWriting(boost::asio::yield_context yield);

  boost::beast::websocket::stream<boost::beast::tcp_stream> ws;
  boost::asio::yield_context yield;

  Handlers handlers_;

  std::queue<std::vector<char>> write_queue_;
  bool writing_ = false;
};

void Fail(boost::beast::error_code ec, char const* what) {
  // std::cerr << what << ": " << ec.message() << "\n";
}

void WebSocketTransport::ConnectionCore::Open(const Handlers& handlers) {
  auto ref = shared_from_this();

  handlers_ = handlers;

  boost::beast::error_code ec;

  for (;;) {
    boost::beast::flat_buffer buffer;

    // Read a message
    ws.async_read(buffer, yield[ec]);

    // This indicates that the session was closed
    if (ec == boost::beast::websocket::error::closed)
      break;

    if (ec) {
      if (handlers_.on_close)
        handlers_.on_close(net::ERR_ABORTED);
      return;
    }

    if (handlers_.on_message) {
      handlers_.on_message({static_cast<const char*>(buffer.data().data()),
                            buffer.data().size()});
    }
  }

  if (handlers_.on_close)
    handlers_.on_close(net::OK);
}

void WebSocketTransport::ConnectionCore::Close() {
  handlers_ = {};

  boost::beast::error_code ec;
  ws.close(boost::beast::websocket::close_code::normal, ec);
}

int WebSocketTransport::ConnectionCore::Write(const void* data, size_t len) {
  write_queue_.push(std::vector<char>(static_cast<const char*>(data),
                                      static_cast<const char*>(data) + len));

  if (!writing_) {
    writing_ = true;
    boost::asio::spawn(ws.get_executor(),
                       [this, ref = shared_from_this()](
                           boost::asio::yield_context yield) mutable {
                         StartWriting(std::move(yield));
                         writing_ = false;
                       });
  }

  return static_cast<int>(len);
}

void WebSocketTransport::ConnectionCore::StartWriting(
    boost::asio::yield_context yield) {
  while (!write_queue_.empty()) {
    auto message = std::move(write_queue_.front());
    write_queue_.pop();

    boost::beast::error_code ec;
    ws.async_write(boost::asio::buffer(message), yield[ec]);
    if (ec) {
      if (handlers_.on_close)
        handlers_.on_close(net::ERR_ABORTED);
      return;
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
  virtual Error Open(const Handlers& handlers) override;
  virtual void Close() override;
  virtual int Read(std::span<char> data) override { return OK; }
  virtual int Write(std::span<const char> data) override;
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

Error WebSocketTransport::Connection::Open(const Handlers& handlers) {
  assert(!opened_);
  opened_ = true;
  core_->Open(handlers);
  return OK;
}

void WebSocketTransport::Connection::Close() {
  opened_ = false;
  core_->Close();
}

int WebSocketTransport::Connection::Write(std::span<const char> data) {
  assert(opened_);
  return core_->Write(data.data(), data.size());
}

// WebSocketTransport::Core

class WebSocketTransport::Core : public std::enable_shared_from_this<Core> {
 public:
  Core(boost::asio::io_context& io_context, std::string host, int port);

  Error Open(const Handlers& handlers);
  void Close();

  void Listen(boost::asio::yield_context yield);
  void DoSession(boost::beast::websocket::stream<boost::beast::tcp_stream> ws,
                 boost::asio::yield_context yield);

 private:
  boost::asio::io_context& io_context_;
  const std::string host_;
  const int port_;

  Handlers handlers_;
};

WebSocketTransport::Core::Core(boost::asio::io_context& io_context,
                               std::string host,
                               int port)
    : io_context_{io_context}, host_{std::move(host)}, port_{std::move(port)} {}

Error WebSocketTransport::Core::Open(const Handlers& handlers) {
  handlers_ = handlers;

  boost::asio::spawn(io_context_,
                     std::bind_front(&Core::Listen, shared_from_this()));

  return Error::OK;
}

void WebSocketTransport::Core::Close() {
  handlers_ = {};
}

void WebSocketTransport::Core::Listen(boost::asio::yield_context yield) {
  auto const address = boost::asio::ip::make_address(host_);
  auto const port = static_cast<unsigned short>(port_);
  const boost::asio::ip::tcp::endpoint endpoint{address, port};

  boost::beast::error_code ec;

  // Open the acceptor
  boost::asio::ip::tcp::acceptor acceptor(io_context_);
  acceptor.open(endpoint.protocol(), ec);
  if (ec)
    return Fail(ec, "open");

  // Allow address reuse
  acceptor.set_option(boost::asio::socket_base::reuse_address(true), ec);
  if (ec)
    return Fail(ec, "set_option");

  // Bind to the server address
  acceptor.bind(endpoint, ec);
  if (ec)
    return Fail(ec, "bind");

  // Start listening for connections
  acceptor.listen(boost::asio::socket_base::max_listen_connections, ec);
  if (ec)
    return Fail(ec, "listen");

  for (;;) {
    boost::asio::ip::tcp::socket socket(io_context_);
    acceptor.async_accept(socket, yield[ec]);
    if (ec)
      Fail(ec, "accept");
    else
      boost::asio::spawn(
          acceptor.get_executor(),
          [this, ref = shared_from_this(), socket = std::move(socket)](
              boost::asio::yield_context yield) mutable {
            DoSession(
                boost::beast::websocket::stream<boost::beast::tcp_stream>{
                    std::move(socket)},
                std::move(yield));
          });
  }
}

void WebSocketTransport::Core::DoSession(
    boost::beast::websocket::stream<boost::beast::tcp_stream> ws,
    boost::asio::yield_context yield) {
  boost::beast::error_code ec;

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
  ws.async_accept(yield[ec]);
  if (ec)
    return Fail(ec, "accept");

  if (handlers_.on_accept) {
    auto connection_core =
        std::make_shared<ConnectionCore>(std::move(ws), std::move(yield));
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

Error WebSocketTransport::Open(const Handlers& handlers) {
  assert(core_);
  return core_->Open(handlers);
}

void WebSocketTransport::Close() {
  core_->Close();
  core_ = nullptr;
}

}  // namespace net
