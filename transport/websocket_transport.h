#pragma once

#include "transport/any_transport.h"
#include "transport/log.h"
#include "transport/transport.h"

#include <boost/asio/as_tuple.hpp>
#include <boost/asio/experimental/channel.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/websocket/ssl.hpp>
#include <boost/beast/websocket/stream.hpp>

#include <cstring>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <queue>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace transport {

using WebSocketClientRequest = boost::beast::websocket::request_type;
using WebSocketClientResponse = boost::beast::websocket::response_type;
using WebSocketServerRequest =
    boost::beast::http::request<boost::beast::http::string_body>;

struct WebSocketServerTlsConfig {
  std::string certificate_chain_pem;
  std::string private_key_pem;
  std::string private_key_passphrase;
};

struct WebSocketClientTlsConfig {
  bool verify_peer = false;
  std::string server_name;
  std::string ca_certificate_pem;
};

struct WebSocketClientOptions {
  std::optional<WebSocketClientTlsConfig> tls;
  std::string path = "/";
  std::function<void(WebSocketClientRequest&)> request_callback;
  std::function<std::optional<error_code>(const WebSocketClientResponse&)>
      response_validator;
  bool enable_permessage_deflate = false;
};

struct WebSocketServerReject {
  boost::beast::http::status status = boost::beast::http::status::bad_request;
  std::string body;
  std::vector<std::pair<std::string, std::string>> headers;
};

struct WebSocketServerOptions {
  std::optional<WebSocketServerTlsConfig> tls;
  std::function<std::optional<WebSocketServerReject>(const WebSocketServerRequest&)>
      handshake_callback;
  bool enable_permessage_deflate = false;
  std::vector<std::pair<std::string, std::string>> response_headers;
};

class WebSocketTransport final : public Transport {
 public:
  WebSocketTransport(const executor& executor,
                     const log_source& log,
                     std::string host,
                     std::string service,
                     bool active,
                     WebSocketServerOptions server_options = {},
                     WebSocketClientOptions client_options = {});
  template <typename WebSocketStream>
  explicit WebSocketTransport(WebSocketStream websocket)
      : executor_{websocket.get_executor()},
        resolver_{executor_},
        acceptor_{executor_},
        accept_channel_{executor_, std::numeric_limits<size_t>::max()},
        mode_{Mode::CONNECTED},
        connected_{true},
        core_{std::make_unique<CoreImpl<WebSocketStream>>(std::move(websocket))} {}

  [[nodiscard]] awaitable<error_code> open() override;
  [[nodiscard]] awaitable<error_code> close() override;
  [[nodiscard]] awaitable<expected<any_transport>> accept() override;
  [[nodiscard]] awaitable<expected<size_t>> read(std::span<char> data) override;
  [[nodiscard]] awaitable<expected<size_t>> write(
      std::span<const char> data) override;

  [[nodiscard]] std::string name() const override;
  [[nodiscard]] bool message_oriented() const override { return true; }
  [[nodiscard]] bool connected() const override { return connected_; }
  [[nodiscard]] bool active() const override { return mode_ == Mode::ACTIVE; }
  [[nodiscard]] executor get_executor() override { return executor_; }

 private:
  using AcceptChannel =
      boost::asio::experimental::channel<void(boost::system::error_code)>;

  enum class Mode { ACTIVE, PASSIVE, CONNECTED };

  class Core {
   public:
    virtual ~Core() = default;

    [[nodiscard]] virtual awaitable<error_code> close() = 0;
    [[nodiscard]] virtual awaitable<expected<size_t>> read(
        std::span<char> data) = 0;
    [[nodiscard]] virtual awaitable<expected<size_t>> write(
        std::span<const char> data) = 0;
  };

  template <typename WebSocketStream>
  class CoreImpl final : public Core {
   public:
    explicit CoreImpl(WebSocketStream websocket)
        : websocket_{std::move(websocket)} {}

    [[nodiscard]] awaitable<error_code> close() override;
    [[nodiscard]] awaitable<expected<size_t>> read(
        std::span<char> data) override;
    [[nodiscard]] awaitable<expected<size_t>> write(
        std::span<const char> data) override;

   private:
    WebSocketStream websocket_;
  };

  [[nodiscard]] awaitable<error_code> OpenActive();
  [[nodiscard]] awaitable<error_code> OpenPassive();
  [[nodiscard]] awaitable<void> AcceptLoop();
  template <typename WebSocketStream>
  void ApplyClientOptions(WebSocketStream& websocket);
  template <typename NextLayer>
  [[nodiscard]] awaitable<error_code> OpenConnectedClient(
      boost::beast::websocket::stream<NextLayer> websocket,
      const std::string& handshake_host);
  template <typename Stream>
  [[nodiscard]] awaitable<void> RejectRequest(
      Stream& stream,
      boost::beast::http::status status,
      std::string body,
      const std::vector<std::pair<std::string, std::string>>& headers);
  template <typename NextLayer>
  [[nodiscard]] awaitable<std::optional<any_transport>> AcceptUpgradedStream(
      boost::beast::websocket::stream<NextLayer> websocket);

  executor executor_;
  log_source log_;
  std::string host_;
  std::string service_;
  WebSocketServerOptions server_options_;
  WebSocketClientOptions client_options_;
  Mode mode_ = Mode::PASSIVE;
  bool connected_ = false;
  bool closed_ = false;

  boost::asio::ip::tcp::resolver resolver_;
  boost::asio::ip::tcp::acceptor acceptor_;
  std::optional<boost::asio::ssl::context> ssl_context_;
  std::unique_ptr<Core> core_;
  std::queue<any_transport> accepted_;
  AcceptChannel accept_channel_;
};

template <typename WebSocketStream>
awaitable<error_code> WebSocketTransport::CoreImpl<WebSocketStream>::close() {
  auto [ec] = co_await websocket_.async_close(
      boost::beast::websocket::close_code::normal,
      boost::asio::as_tuple(boost::asio::use_awaitable));
  if (ec == boost::beast::websocket::error::closed)
    co_return OK;
  if (!ec)
    co_return OK;
  co_return ec;
}

template <typename WebSocketStream>
awaitable<expected<size_t>> WebSocketTransport::CoreImpl<WebSocketStream>::read(
    std::span<char> data) {
  boost::beast::flat_buffer buffer;
  auto [ec, _] = co_await websocket_.async_read(
      buffer, boost::asio::as_tuple(boost::asio::use_awaitable));
  if (ec == boost::beast::websocket::error::closed)
    co_return size_t{0};
  if (ec)
    co_return ec;

  const auto size = buffer.size();
  if (size > data.size())
    co_return ERR_INVALID_ARGUMENT;

  const auto buffer_data = buffer.data();
  std::memcpy(data.data(), buffer_data.data(), size);
  co_return size;
}

template <typename WebSocketStream>
awaitable<expected<size_t>> WebSocketTransport::CoreImpl<WebSocketStream>::write(
    std::span<const char> data) {
  websocket_.text(true);
  auto [ec, written] = co_await websocket_.async_write(
      boost::asio::buffer(data.data(), data.size()),
      boost::asio::as_tuple(boost::asio::use_awaitable));
  if (ec)
    co_return ec;
  co_return written;
}

template <typename WebSocketStream>
void WebSocketTransport::ApplyClientOptions(WebSocketStream& websocket) {
  namespace websocket_ns = boost::beast::websocket;

  if (client_options_.enable_permessage_deflate) {
    websocket_ns::permessage_deflate options;
    options.client_enable = true;
    options.server_enable = true;
    websocket.set_option(options);
  }
  if (client_options_.request_callback) {
    websocket.set_option(websocket_ns::stream_base::decorator(
        [callback = client_options_.request_callback](
            WebSocketClientRequest& request) { callback(request); }));
  }
}

template <typename NextLayer>
awaitable<error_code> WebSocketTransport::OpenConnectedClient(
    boost::beast::websocket::stream<NextLayer> websocket,
    const std::string& handshake_host) {
  ApplyClientOptions(websocket);

  WebSocketClientResponse response;
  auto [handshake_error] = co_await websocket.async_handshake(
      response,
      handshake_host,
      client_options_.path,
      boost::asio::as_tuple(boost::asio::use_awaitable));
  if (handshake_error)
    co_return handshake_error;

  if (client_options_.response_validator) {
    auto validation_error = client_options_.response_validator(response);
    if (validation_error.has_value()) {
      boost::system::error_code ignored;
      if constexpr (std::is_same_v<NextLayer, boost::asio::ip::tcp::socket>) {
        websocket.next_layer().close(ignored);
      } else {
        websocket.next_layer().next_layer().close(ignored);
      }
      co_return *validation_error;
    }
  }

  core_ = std::make_unique<CoreImpl<boost::beast::websocket::stream<NextLayer>>>(
      std::move(websocket));
  connected_ = true;
  closed_ = false;
  co_return OK;
}

template <typename Stream>
awaitable<void> WebSocketTransport::RejectRequest(
    Stream& stream,
    boost::beast::http::status status,
    std::string body,
    const std::vector<std::pair<std::string, std::string>>& headers) {
  boost::beast::http::response<boost::beast::http::string_body> response{
      status, 11};
  response.set(boost::beast::http::field::content_type, "text/plain");
  for (const auto& [name, value] : headers)
    response.set(name, value);
  response.body() = std::move(body);
  response.prepare_payload();

  auto [ec, _] = co_await boost::beast::http::async_write(
      stream, response, boost::asio::as_tuple(boost::asio::use_awaitable));
  if (!ec) {
    boost::system::error_code ignored;
    if constexpr (std::is_same_v<Stream, boost::asio::ip::tcp::socket>) {
      stream.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ignored);
    } else {
      stream.next_layer().shutdown(
          boost::asio::ip::tcp::socket::shutdown_both, ignored);
    }
  }
}

template <typename NextLayer>
awaitable<std::optional<any_transport>> WebSocketTransport::AcceptUpgradedStream(
    boost::beast::websocket::stream<NextLayer> websocket) {
  namespace http = boost::beast::http;
  namespace websocket_ns = boost::beast::websocket;

  auto& next_layer = websocket.next_layer();

  boost::beast::flat_buffer buffer;
  WebSocketServerRequest request;
  auto [read_ec, _] = co_await http::async_read(
      next_layer, buffer, request,
      boost::asio::as_tuple(boost::asio::use_awaitable));
  if (read_ec)
    co_return std::nullopt;

  if (!websocket_ns::is_upgrade(request)) {
    co_await RejectRequest(
        next_layer,
        http::status::bad_request,
        "WebSocket upgrade required",
        {});
    co_return std::nullopt;
  }

  if (server_options_.handshake_callback) {
    auto rejection = server_options_.handshake_callback(request);
    if (rejection.has_value()) {
      co_await RejectRequest(next_layer,
                             rejection->status,
                             std::move(rejection->body),
                             rejection->headers);
      co_return std::nullopt;
    }
  }

  websocket.set_option(websocket_ns::stream_base::timeout::suggested(
      boost::beast::role_type::server));
  if (server_options_.enable_permessage_deflate) {
    websocket_ns::permessage_deflate options;
    options.server_enable = true;
    options.client_enable = true;
    websocket.set_option(options);
  }
  if (!server_options_.response_headers.empty()) {
    websocket.set_option(websocket_ns::stream_base::decorator(
        [headers = server_options_.response_headers](
            websocket_ns::response_type& response) {
          for (const auto& [name, value] : headers)
            response.set(name, value);
        }));
  }

  auto [accept_ec] = co_await websocket.async_accept(
      request, boost::asio::as_tuple(boost::asio::use_awaitable));
  if (accept_ec)
    co_return std::nullopt;

  co_return any_transport{
      std::make_unique<WebSocketTransport>(std::move(websocket))};
}

}  // namespace transport
