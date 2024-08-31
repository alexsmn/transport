#include "transport/websocket_transport.h"

#include <boost/asio/as_tuple.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <queue>

namespace transport {

class WebSocketTransport::ConnectionCore
    : public std::enable_shared_from_this<ConnectionCore> {
 public:
  ConnectionCore(boost::beast::websocket::stream<boost::beast::tcp_stream> ws)
      : ws{std::move(ws)} {}

  executor get_executor() { return ws.get_executor(); }

  [[nodiscard]] awaitable<error_code> Open();
  [[nodiscard]] awaitable<error_code> Close();

  [[nodiscard]] awaitable<expected<size_t>> Write(std::span<const char> data);

 private:
  [[nodiscard]] awaitable<void> StartReading();
  [[nodiscard]] awaitable<void> StartWriting();

  boost::beast::websocket::stream<boost::beast::tcp_stream> ws;

  std::queue<std::vector<char>> write_queue_;
  bool writing_ = false;
};

awaitable<error_code> WebSocketTransport::ConnectionCore::Open() {
  boost::asio::co_spawn(
      ws.get_executor(),
      std::bind_front(&ConnectionCore::StartReading, shared_from_this()),
      boost::asio::detached);

  co_return OK;
}

awaitable<void> WebSocketTransport::ConnectionCore::StartReading() {
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
      co_return;
    }

    /* if (handlers_.on_message) {
      handlers_.on_message({static_cast<const char*>(buffer.data().data()),
                            buffer.data().size()});
    }*/
  }
}

awaitable<error_code> WebSocketTransport::ConnectionCore::Close() {
  co_await ws.async_close(boost::beast::websocket::close_code::normal,
                          boost::asio::as_tuple(boost::asio::use_awaitable));
  co_return OK;
}

awaitable<expected<size_t>> WebSocketTransport::ConnectionCore::Write(
    std::span<const char> data) {
  auto data_size = data.size();
  write_queue_.emplace(data.begin(), data.end());

  if (!writing_) {
    writing_ = true;
    boost::asio::co_spawn(ws.get_executor(), StartWriting(),
                          boost::asio::detached);
  }

  // TODO: Proper async.
  co_return data_size;
}

awaitable<void> WebSocketTransport::ConnectionCore::StartWriting() {
  while (!write_queue_.empty()) {
    auto message = std::move(write_queue_.front());
    write_queue_.pop();

    auto [ec, _] = co_await ws.async_write(
        boost::asio::buffer(message),
        boost::asio::as_tuple(boost::asio::use_awaitable));

    if (ec) {
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
  [[nodiscard]] virtual awaitable<error_code> open() override;
  [[nodiscard]] virtual awaitable<error_code> close() override;
  [[nodiscard]] virtual awaitable<expected<any_transport>> accept() override;

  [[nodiscard]] virtual awaitable<expected<size_t>> read(
      std::span<char> data) override {
    co_return ERR_NOT_IMPLEMENTED;
  }

  [[nodiscard]] virtual awaitable<expected<size_t>> write(
      std::span<const char> data) override;

  virtual std::string name() const override { return "WebSocket"; }
  virtual bool message_oriented() const override { return true; }
  virtual bool connected() const override { return true; }
  virtual bool active() const override { return false; }
  virtual executor get_executor() override;

 private:
  const std::shared_ptr<ConnectionCore> core_;
  bool opened_ = false;
};

WebSocketTransport::Connection::~Connection() {
  if (opened_) {
    boost::asio::co_spawn(
        core_->get_executor(), [core = core_] { return core->Close(); },
        boost::asio::detached);
  }
}

awaitable<error_code> WebSocketTransport::Connection::open() {
  assert(!opened_);

  opened_ = true;

  return core_->Open();
}

awaitable<error_code> WebSocketTransport::Connection::close() {
  NET_CO_RETURN_IF_ERROR(co_await core_->Close());
  opened_ = false;
  co_return OK;
}

awaitable<expected<any_transport>> WebSocketTransport::Connection::accept() {
  co_return ERR_ACCESS_DENIED;
}

awaitable<expected<size_t>> WebSocketTransport::Connection::write(
    std::span<const char> data) {
  assert(opened_);
  return core_->Write(data);
}

executor WebSocketTransport::Connection::get_executor() {
  return core_->get_executor();
}

// WebSocketTransport::Core

class WebSocketTransport::Core : public std::enable_shared_from_this<Core> {
 public:
  Core(const executor& executor, std::string host, int port);

  executor get_executor() const { return executor_; }

  [[nodiscard]] awaitable<error_code> Open();
  [[nodiscard]] awaitable<error_code> Close();

  [[nodiscard]] awaitable<expected<any_transport>> Accept();

  [[nodiscard]] awaitable<void> StartAccepting();

  [[nodiscard]] awaitable<void> DoSession(
      boost::beast::websocket::stream<boost::beast::tcp_stream> ws);

 private:
  executor executor_;
  const std::string host_;
  const int port_;

  boost::asio::ip::tcp::acceptor acceptor_{executor_};
};

WebSocketTransport::Core::Core(const executor& executor,
                               std::string host,
                               int port)
    : executor_{executor}, host_{std::move(host)}, port_{port} {}

awaitable<error_code> WebSocketTransport::Core::Open() {
  auto const address = boost::asio::ip::make_address(host_);
  auto const port = static_cast<unsigned short>(port_);
  const boost::asio::ip::tcp::endpoint endpoint{address, port};

  boost::beast::error_code ec;

  // Open the acceptor
  acceptor_.open(endpoint.protocol(), ec);
  if (ec) {
    co_return ec;
  }

  // Allow address reuse
  acceptor_.set_option(boost::asio::socket_base::reuse_address(true), ec);
  if (ec) {
    co_return ec;
  }

  // Bind to the server address
  acceptor_.bind(endpoint, ec);
  if (ec) {
    co_return ec;
  }

  // Start listening for connections
  acceptor_.listen(boost::asio::socket_base::max_listen_connections, ec);
  if (ec) {
    co_return ec;
  }

  boost::asio::co_spawn(
      executor_, std::bind_front(&Core::StartAccepting, shared_from_this()),
      boost::asio::detached);

  co_return OK;
}

awaitable<error_code> WebSocketTransport::Core::Close() {
  co_return OK;
}

awaitable<expected<any_transport>> WebSocketTransport::Core::Accept() {
  // TODO: Implement.
  co_return ERR_ACCESS_DENIED;
}

awaitable<void> WebSocketTransport::Core::StartAccepting() {
  for (;;) {
    boost::asio::ip::tcp::socket socket{executor_};

    auto [ec] = co_await acceptor_.async_accept(
        socket, boost::asio::as_tuple(boost::asio::use_awaitable));

    if (ec) {
      co_return;
    }

    boost::asio::co_spawn(
        executor_,
        DoSession(boost::beast::websocket::stream<boost::beast::tcp_stream>{
            std::move(socket)}),
        boost::asio::detached);
  }
}

awaitable<void> WebSocketTransport::Core::DoSession(
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

  /*if (handlers_.on_accept) {
    auto connection_core = std::make_shared<ConnectionCore>(std::move(ws));
    handlers_.on_accept(std::make_unique<Connection>(connection_core));
  }*/
}

// WebSocketTransport

WebSocketTransport::WebSocketTransport(const executor& executor,
                                       std::string host,
                                       int port)
    : core_{std::make_shared<Core>(executor, std::move(host), port)} {}

WebSocketTransport::~WebSocketTransport() {
  if (core_) {
    boost::asio::co_spawn(
        core_->get_executor(), [core = core_] { return core->Close(); },
        boost::asio::detached);
  }
}

awaitable<error_code> WebSocketTransport::open() {
  assert(core_);
  return core_->Open();
}

awaitable<error_code> WebSocketTransport::close() {
  auto core = std::exchange(core_, nullptr);
  co_await core->Close();
  co_return OK;
}

executor WebSocketTransport::get_executor() {
  return core_->get_executor();
}

awaitable<expected<any_transport>> WebSocketTransport::accept() {
  co_return co_await core_->Accept();
}

}  // namespace transport
