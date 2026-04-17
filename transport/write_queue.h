#pragma once

#include "transport/awaitable.h"
#include "transport/expected.h"

#include <boost/asio/experimental/channel.hpp>
#include <memory>
#include <span>

namespace transport {

class any_transport;

class WriteQueue {
 public:
  explicit WriteQueue(any_transport& transport);

  void BlindWrite(std::span<const char> data);

  awaitable<expected<size_t>> Write(std::span<const char> data);

 private:
  using Channel =
      boost::asio::experimental::channel<void(boost::system::error_code)>;

  struct State {
    explicit State(any_transport& transport) : transport{&transport} {}

    any_transport* transport;
    std::shared_ptr<Channel> last_write;
    std::shared_ptr<bool> cancelation = std::make_shared<bool>();
  };

  static awaitable<expected<size_t>> Write(
      const std::shared_ptr<State>& state,
      std::span<const char> data);

  std::shared_ptr<State> state_;
};

}  // namespace transport
