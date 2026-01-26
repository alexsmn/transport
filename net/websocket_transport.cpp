#include "net/websocket_transport.h"

#include "net/transport_util.h"

#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <queue>

namespace net {

// WebSocketTransport::ConnectionCore

class WebSocketTransport::ConnectionCore
    : public std::enable_shared_from_this<ConnectionCore> {
 public:
  explicit ConnectionCore(
      boost::beast::websocket::stream<boost::beast::tcp_stream> ws)
      : ws_{std::move(ws)} {}

  void Open(const Handlers& handlers);
  void Close();

  promise<size_t> Write(const void* data, size_t len);

 private:
  void StartReading();
  void StartWriting();

  boost::beast::websocket::stream<boost::beast::tcp_stream> ws_;
  boost::beast::flat_buffer read_buffer_;

  Handlers handlers_;
  bool closed_ = false;

  std::queue<std::vector<char>> write_queue_;
  bool writing_ = false;
};

void WebSocketTransport::ConnectionCore::Open(const Handlers& handlers) {
  handlers_ = handlers;
  StartReading();
}

void WebSocketTransport::ConnectionCore::Close() {
  if (closed_) {
    return;
  }
  closed_ = true;
  handlers_ = {};

  boost::beast::error_code ec;
  ws_.close(boost::beast::websocket::close_code::normal, ec);
}

void WebSocketTransport::ConnectionCore::StartReading() {
  ws_.async_read(read_buffer_, [this, ref = shared_from_this()](
                                   boost::beast::error_code ec,
                                   std::size_t bytes_transferred) {
    if (closed_) {
      return;
    }

    if (ec == boost::beast::websocket::error::closed) {
      if (handlers_.on_close) {
        handlers_.on_close(net::OK);
      }
      return;
    }

    if (ec) {
      if (handlers_.on_close) {
        handlers_.on_close(net::ERR_ABORTED);
      }
      return;
    }

    if (handlers_.on_message) {
      handlers_.on_message({static_cast<const char*>(read_buffer_.data().data()),
                            read_buffer_.data().size()});
    }

    read_buffer_.consume(bytes_transferred);
    StartReading();
  });
}

promise<size_t> WebSocketTransport::ConnectionCore::Write(const void* data,
                                                          size_t len) {
  write_queue_.emplace(static_cast<const char*>(data),
                       static_cast<const char*>(data) + len);

  if (!writing_) {
    StartWriting();
  }

  // TODO: Proper async.
  return make_resolved_promise(len);
}

void WebSocketTransport::ConnectionCore::StartWriting() {
  if (closed_ || write_queue_.empty()) {
    writing_ = false;
    return;
  }

  writing_ = true;
  auto& message = write_queue_.front();

  ws_.async_write(
      boost::asio::buffer(message),
      [this, ref = shared_from_this()](boost::beast::error_code ec,
                                       std::size_t /*bytes_transferred*/) {
        if (closed_) {
          return;
        }

        write_queue_.pop();

        if (ec) {
          if (handlers_.on_close) {
            handlers_.on_close(net::ERR_ABORTED);
          }
          return;
        }

        StartWriting();
      });
}

// WebSocketTransport::Connection

class WebSocketTransport::Connection : public Transport {
 public:
  explicit Connection(std::shared_ptr<ConnectionCore> core)
      : core_{std::move(core)} {}
  ~Connection();

  // Transport
  virtual promise<void> Open(const Handlers& handlers) override;
  virtual void Close() override;
  virtual int Read(std::span<char> data) override { return OK; }
  virtual promise<size_t> Write(std::span<const char> data) override;
  virtual std::string GetName() const override { return "WebSocket"; }
  virtual bool IsMessageOriented() const override { return true; }
  virtual bool IsConnected() const override { return true; }
  virtual bool IsActive() const override { return false; }

 private:
  const std::shared_ptr<ConnectionCore> core_;
  bool opened_ = false;
};

WebSocketTransport::Connection::~Connection() {
  if (opened_) {
    core_->Close();
  }
}

promise<void> WebSocketTransport::Connection::Open(const Handlers& handlers) {
  assert(!opened_);

  opened_ = true;

  auto [p, promise_handlers] = MakePromiseHandlers(handlers);
  core_->Open(promise_handlers);

  // Resolve immediately since connection is already established
  if (handlers.on_open) {
    handlers.on_open();
  }
  return make_resolved_promise();
}

void WebSocketTransport::Connection::Close() {
  opened_ = false;
  core_->Close();
}

promise<size_t> WebSocketTransport::Connection::Write(
    std::span<const char> data) {
  assert(opened_);
  return core_->Write(data.data(), data.size());
}

// WebSocketTransport::Core

class WebSocketTransport::Core : public std::enable_shared_from_this<Core> {
 public:
  Core(boost::asio::io_context& io_context, std::string host, int port);

  void Open(const Handlers& handlers);
  void Close();

 private:
  void StartAccepting();
  void OnAccept(boost::beast::error_code ec,
                boost::asio::ip::tcp::socket socket);
  void DoHandshake(boost::beast::websocket::stream<boost::beast::tcp_stream> ws);

  boost::asio::io_context& io_context_;
  const std::string host_;
  const int port_;

  std::unique_ptr<boost::asio::ip::tcp::acceptor> acceptor_;
  Handlers handlers_;
  bool closed_ = false;
};

WebSocketTransport::Core::Core(boost::asio::io_context& io_context,
                               std::string host,
                               int port)
    : io_context_{io_context}, host_{std::move(host)}, port_{std::move(port)} {}

void WebSocketTransport::Core::Open(const Handlers& handlers) {
  handlers_ = handlers;

  auto const address = boost::asio::ip::make_address(host_);
  auto const port = static_cast<unsigned short>(port_);
  const boost::asio::ip::tcp::endpoint endpoint{address, port};

  boost::beast::error_code ec;

  acceptor_ = std::make_unique<boost::asio::ip::tcp::acceptor>(io_context_);

  acceptor_->open(endpoint.protocol(), ec);
  if (ec) {
    if (handlers_.on_close) {
      handlers_.on_close(net::ERR_FAILED);
    }
    return;
  }

  acceptor_->set_option(boost::asio::socket_base::reuse_address(true), ec);
  if (ec) {
    if (handlers_.on_close) {
      handlers_.on_close(net::ERR_FAILED);
    }
    return;
  }

  acceptor_->bind(endpoint, ec);
  if (ec) {
    if (handlers_.on_close) {
      handlers_.on_close(net::ERR_FAILED);
    }
    return;
  }

  acceptor_->listen(boost::asio::socket_base::max_listen_connections, ec);
  if (ec) {
    if (handlers_.on_close) {
      handlers_.on_close(net::ERR_FAILED);
    }
    return;
  }

  if (handlers_.on_open) {
    handlers_.on_open();
  }

  StartAccepting();
}

void WebSocketTransport::Core::Close() {
  if (closed_) {
    return;
  }
  closed_ = true;
  handlers_ = {};

  if (acceptor_) {
    boost::beast::error_code ec;
    acceptor_->close(ec);
  }
}

void WebSocketTransport::Core::StartAccepting() {
  if (closed_ || !acceptor_) {
    return;
  }

  acceptor_->async_accept(
      [this, ref = shared_from_this()](boost::beast::error_code ec,
                                       boost::asio::ip::tcp::socket socket) {
        OnAccept(ec, std::move(socket));
      });
}

void WebSocketTransport::Core::OnAccept(boost::beast::error_code ec,
                                        boost::asio::ip::tcp::socket socket) {
  if (closed_) {
    return;
  }

  if (ec) {
    // Continue accepting on error
    StartAccepting();
    return;
  }

  DoHandshake(boost::beast::websocket::stream<boost::beast::tcp_stream>{
      std::move(socket)});
  StartAccepting();
}

void WebSocketTransport::Core::DoHandshake(
    boost::beast::websocket::stream<boost::beast::tcp_stream> ws) {
  auto ws_ptr =
      std::make_shared<boost::beast::websocket::stream<boost::beast::tcp_stream>>(
          std::move(ws));

  ws_ptr->binary(true);

  ws_ptr->set_option(boost::beast::websocket::stream_base::timeout::suggested(
      boost::beast::role_type::server));

  ws_ptr->set_option(boost::beast::websocket::stream_base::decorator(
      [](boost::beast::websocket::response_type& res) {
        res.set(boost::beast::http::field::server,
                std::string(BOOST_BEAST_VERSION_STRING) + " websocket-server");
      }));

  ws_ptr->async_accept(
      [this, ref = shared_from_this(), ws_ptr](boost::beast::error_code ec) {
        if (closed_) {
          return;
        }

        if (ec) {
          return;
        }

        if (handlers_.on_accept) {
          auto connection_core =
              std::make_shared<ConnectionCore>(std::move(*ws_ptr));
          handlers_.on_accept(std::make_unique<Connection>(connection_core));
        }
      });
}

// WebSocketTransport

WebSocketTransport::WebSocketTransport(boost::asio::io_context& io_context,
                                       std::string host,
                                       int port)
    : core_{std::make_shared<Core>(io_context, std::move(host), port)} {}

WebSocketTransport::~WebSocketTransport() {
  if (core_) {
    core_->Close();
  }
}

promise<void> WebSocketTransport::Open(const Handlers& handlers) {
  assert(core_);

  auto [p, promise_handlers] = MakePromiseHandlers(handlers);
  core_->Open(promise_handlers);
  return p;
}

void WebSocketTransport::Close() {
  core_->Close();
  core_ = nullptr;
}

}  // namespace net
