#pragma once

#include "transport/awaitable.h"
#include "transport/error_or.h"

#include <boost/asio/experimental/channel.hpp>
#include <memory>
#include <span>

namespace transport {

class any_transport;

class WriteQueue {
 public:
  explicit WriteQueue(any_transport& transport) : transport_{transport} {}

  void BlindWrite(std::span<const char> data);

  awaitable<ErrorOr<size_t>> Write(std::span<const char> data);

 private:
  any_transport& transport_;

  using Channel =
      boost::asio::experimental::channel<void(boost::system::error_code)>;

  std::shared_ptr<Channel> last_write_;

  std::shared_ptr<bool> cancelation_ = std::make_shared<bool>();
};

}  // namespace transport