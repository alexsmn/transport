#pragma once

#include "transport/any_transport.h"
#include "transport/log.h"
#include "transport/transport.h"

#include <boost/asio/as_tuple.hpp>
#include <boost/asio/experimental/channel.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/websocket/ssl.hpp>
#include <boost/beast/websocket/stream.hpp>

#include <cstring>
#include <limits>
#include <memory>
#include <queue>
#include <string>
#include <utility>

namespace transport {

class WebSocketTransport final : public Transport {
 public:
  WebSocketTransport(const executor& executor,
                     const log_source& log,
                     std::string host,
                     std::string service,
                     bool active);
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

  executor executor_;
  log_source log_;
  std::string host_;
  std::string service_;
  Mode mode_ = Mode::PASSIVE;
  bool connected_ = false;
  bool closed_ = false;

  boost::asio::ip::tcp::resolver resolver_;
  boost::asio::ip::tcp::acceptor acceptor_;
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

}  // namespace transport
