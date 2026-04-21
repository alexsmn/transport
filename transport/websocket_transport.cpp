#include "transport/websocket_transport.h"

#include <boost/asio/buffer.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/ssl/context_base.hpp>
#include <limits>
#include <openssl/ssl.h>

namespace transport {
namespace {

namespace websocket = boost::beast::websocket;

error_code ConfigureServerTlsContext(
    boost::asio::ssl::context& context,
    const WebSocketServerTlsConfig& config) {
  boost::system::error_code ec;

  context.set_options(boost::asio::ssl::context::default_workarounds |
                          boost::asio::ssl::context::no_sslv2 |
                          boost::asio::ssl::context::no_sslv3 |
                          boost::asio::ssl::context::single_dh_use,
                      ec);
  if (ec)
    return ec;

  if (!config.private_key_passphrase.empty()) {
    context.set_password_callback(
        [password = config.private_key_passphrase](
            std::size_t,
            boost::asio::ssl::context_base::password_purpose) {
          return password;
        });
  }

  context.use_certificate_chain(
      boost::asio::buffer(config.certificate_chain_pem), ec);
  if (ec)
    return ec;

  context.use_private_key(boost::asio::buffer(config.private_key_pem),
                          boost::asio::ssl::context::file_format::pem, ec);
  if (ec)
    return ec;

  return OK;
}

error_code ConfigureClientTlsContext(
    boost::asio::ssl::context& context,
    const WebSocketClientTlsConfig& config) {
  boost::system::error_code ec;

  context.set_options(boost::asio::ssl::context::default_workarounds |
                          boost::asio::ssl::context::no_sslv2 |
                          boost::asio::ssl::context::no_sslv3,
                      ec);
  if (ec)
    return ec;

  context.set_verify_mode(config.verify_peer ? boost::asio::ssl::verify_peer
                                             : boost::asio::ssl::verify_none,
                          ec);
  if (ec)
    return ec;

  if (!config.ca_certificate_pem.empty()) {
    context.add_certificate_authority(
        boost::asio::buffer(config.ca_certificate_pem), ec);
    if (ec)
      return ec;
  }

  return OK;
}

}  // namespace

WebSocketTransport::WebSocketTransport(const executor& executor,
                                       const log_source& log,
                                       std::string host,
                                       std::string service,
                                       bool active,
                                       WebSocketServerOptions server_options,
                                       WebSocketClientOptions client_options)
    : executor_{executor},
      log_{log},
      host_{std::move(host)},
      service_{std::move(service)},
      server_options_{std::move(server_options)},
      client_options_{std::move(client_options)},
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

  std::string handshake_host = host_ + ":" + service_;
  if (client_options_.tls.has_value()) {
    ssl_context_.emplace(boost::asio::ssl::context::tls_client);
    const auto tls_error =
        ConfigureClientTlsContext(*ssl_context_, *client_options_.tls);
    if (tls_error != OK)
      co_return tls_error;

    websocket::stream<boost::asio::ssl::stream<boost::asio::ip::tcp::socket>>
        websocket{executor_, *ssl_context_};
    auto [connect_error, endpoint] = co_await boost::asio::async_connect(
        websocket.next_layer().next_layer(), results,
        boost::asio::as_tuple(boost::asio::use_awaitable));
    if (connect_error)
      co_return connect_error;

    const auto& server_name = client_options_.tls->server_name.empty()
                                  ? host_
                                  : client_options_.tls->server_name;
    if (!SSL_set_tlsext_host_name(websocket.next_layer().native_handle(),
                                  server_name.c_str())) {
      co_return ERR_FAILED;
    }

    auto [client_handshake_error] = co_await websocket.next_layer().async_handshake(
        boost::asio::ssl::stream_base::client,
        boost::asio::as_tuple(boost::asio::use_awaitable));
    if (client_handshake_error)
      co_return client_handshake_error;

    handshake_host = endpoint.address().to_string() + ":" + std::to_string(endpoint.port());
    co_return co_await OpenConnectedClient(std::move(websocket), handshake_host);
  }

  websocket::stream<boost::asio::ip::tcp::socket> websocket{executor_};
  auto [connect_error, endpoint] = co_await boost::asio::async_connect(
      websocket.next_layer(), results,
      boost::asio::as_tuple(boost::asio::use_awaitable));
  if (connect_error)
    co_return connect_error;

  handshake_host = endpoint.address().to_string() + ":" + std::to_string(endpoint.port());
  co_return co_await OpenConnectedClient(std::move(websocket), handshake_host);
}

awaitable<error_code> WebSocketTransport::OpenPassive() {
  if (server_options_.tls.has_value()) {
    ssl_context_.emplace(boost::asio::ssl::context::tls_server);
    const auto tls_error =
        ConfigureServerTlsContext(*ssl_context_, *server_options_.tls);
    if (tls_error != OK)
      co_return tls_error;
  }

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

    std::optional<any_transport> accepted;
    if (ssl_context_.has_value()) {
      boost::asio::ssl::stream<boost::asio::ip::tcp::socket> tls_stream{
          std::move(socket), *ssl_context_};
      auto [tls_error] = co_await tls_stream.async_handshake(
          boost::asio::ssl::stream_base::server,
          boost::asio::as_tuple(boost::asio::use_awaitable));
      if (tls_error)
        continue;

      accepted = co_await AcceptUpgradedStream(
          websocket::stream<
              boost::asio::ssl::stream<boost::asio::ip::tcp::socket>>{
              std::move(tls_stream)});
    } else {
      accepted = co_await AcceptUpgradedStream(
          websocket::stream<boost::asio::ip::tcp::socket>{std::move(socket)});
    }
    if (!accepted.has_value())
      continue;

    accepted_.push(std::move(*accepted));
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

boost::asio::ip::tcp::endpoint WebSocketTransport::local_endpoint() const {
  boost::system::error_code ec;
  return acceptor_.local_endpoint(ec);
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
