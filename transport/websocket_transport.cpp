#include "transport/websocket_transport.h"

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/detached.hpp>
#include <limits>

namespace transport {
namespace {

namespace websocket = boost::beast::websocket;

}  // namespace

WebSocketTransport::WebSocketTransport(const executor& executor,
                                       const log_source& log,
                                       std::string host,
                                       std::string service,
                                       bool active)
    : executor_{executor},
      log_{log},
      host_{std::move(host)},
      service_{std::move(service)},
      mode_{active ? Mode::ACTIVE : Mode::PASSIVE},
      resolver_{executor},
      acceptor_{executor},
      accept_channel_{executor, std::numeric_limits<size_t>::max()} {}

awaitable<error_code> WebSocketTransport::open() {
  if (connected_) {
    co_return OK;
  }

  log_.write(LogSeverity::Normal, "Open");

  co_return mode_ == Mode::ACTIVE ? co_await OpenActive() : co_await OpenPassive();
}

awaitable<error_code> WebSocketTransport::OpenActive() {
  auto [resolve_error, results] = co_await resolver_.async_resolve(
      host_, service_, boost::asio::as_tuple(boost::asio::use_awaitable));
  if (resolve_error) {
    co_return resolve_error;
  }

  websocket::stream<boost::asio::ip::tcp::socket> websocket{executor_};
  auto [connect_error, endpoint] = co_await boost::asio::async_connect(
      websocket.next_layer(), results,
      boost::asio::as_tuple(boost::asio::use_awaitable));
  if (connect_error) {
    co_return connect_error;
  }

  auto [handshake_error] = co_await websocket.async_handshake(
      endpoint.address().to_string() + ":" + std::to_string(endpoint.port()),
      "/",
      boost::asio::as_tuple(boost::asio::use_awaitable));
  if (handshake_error) {
    co_return handshake_error;
  }

  core_ = std::make_unique<
      CoreImpl<websocket::stream<boost::asio::ip::tcp::socket>>>(
      std::move(websocket));
  connected_ = true;
  closed_ = false;
  co_return OK;
}

awaitable<error_code> WebSocketTransport::OpenPassive() {
  auto [resolve_error, results] = co_await resolver_.async_resolve(
      host_, service_,
      boost::asio::ip::tcp::resolver::passive,
      boost::asio::as_tuple(boost::asio::use_awaitable));
  if (resolve_error) {
    co_return resolve_error;
  }

  boost::system::error_code bind_error = boost::asio::error::fault;
  for (const auto& entry : results) {
    acceptor_.open(entry.endpoint().protocol(), bind_error);
    if (bind_error) {
      continue;
    }

    acceptor_.set_option(boost::asio::ip::tcp::acceptor::reuse_address(true),
                         bind_error);
    if (bind_error) {
      acceptor_.close();
      continue;
    }

    acceptor_.bind(entry.endpoint(), bind_error);
    if (bind_error) {
      acceptor_.close();
      continue;
    }

    acceptor_.listen(boost::asio::socket_base::max_listen_connections,
                     bind_error);
    if (!bind_error) {
      break;
    }

    acceptor_.close();
  }

  if (bind_error) {
    co_return bind_error;
  }

  connected_ = true;
  closed_ = false;
  boost::asio::co_spawn(
      executor_, [this]() { return AcceptLoop(); }, boost::asio::detached);
  co_return OK;
}

awaitable<void> WebSocketTransport::AcceptLoop() {
  while (!closed_) {
    auto [accept_error, socket] = co_await acceptor_.async_accept(
        boost::asio::as_tuple(boost::asio::use_awaitable));
    if (accept_error) {
      co_return;
    }

    websocket::stream<boost::asio::ip::tcp::socket> websocket{
        std::move(socket)};
    auto [handshake_error] = co_await websocket.async_accept(
        boost::asio::as_tuple(boost::asio::use_awaitable));
    if (handshake_error) {
      continue;
    }

    any_transport accepted{
        std::make_unique<WebSocketTransport>(std::move(websocket))};

    accepted_.push(std::move(accepted));
    if (!accept_channel_.try_send(boost::system::error_code{})) {
      log_.write(LogSeverity::Warning, "WebSocket accept queue is full");
      accepted_.pop();
      continue;
    }
  }
}

awaitable<error_code> WebSocketTransport::close() {
  if (closed_) {
    co_return OK;
  }

  closed_ = true;
  connected_ = false;
  resolver_.cancel();

  boost::system::error_code ignored;
  acceptor_.cancel(ignored);
  acceptor_.close(ignored);
  accept_channel_.cancel();

  if (core_) {
    co_return co_await core_->close();
  }

  co_return OK;
}

awaitable<expected<any_transport>> WebSocketTransport::accept() {
  if (mode_ != Mode::PASSIVE) {
    co_return ERR_ACCESS_DENIED;
  }

  while (accepted_.empty()) {
    auto [error] = co_await accept_channel_.async_receive(
        boost::asio::as_tuple(boost::asio::use_awaitable));
    if (error) {
      co_return error == boost::asio::error::operation_aborted ? ERR_ABORTED
                                                               : error;
    }
  }

  auto transport = std::move(accepted_.front());
  accepted_.pop();
  co_return transport;
}

awaitable<expected<size_t>> WebSocketTransport::read(std::span<char> data) {
  if (mode_ == Mode::PASSIVE || !core_) {
    co_return ERR_ACCESS_DENIED;
  }

  auto result = co_await core_->read(data);
  if (result.ok() && *result == 0) {
    connected_ = false;
  }
  co_return result;
}

awaitable<expected<size_t>> WebSocketTransport::write(std::span<const char> data) {
  if (mode_ == Mode::PASSIVE || !core_) {
    co_return ERR_ACCESS_DENIED;
  }

  co_return co_await core_->write(data);
}

std::string WebSocketTransport::name() const {
  switch (mode_) {
    case Mode::ACTIVE:
      return "WebSocket Active";
    case Mode::PASSIVE:
      return "WebSocket Passive";
    case Mode::CONNECTED:
      return "WebSocket Connection";
    default:
      return "WebSocket";
  }
}

}  // namespace transport
