#pragma once

#include "net/awaitable.h"
#include "net/error_or.h"

#include <boost/asio/experimental/channel.hpp>
#include <memory>
#include <span>

namespace net {

class Transport;

class WriteQueue {
 public:
  explicit WriteQueue(Transport& transport) : transport_{transport} {}

  void BlindWrite(std::span<const char> data);

  awaitable<ErrorOr<size_t>> Write(std::span<const char> data);

 private:
  Transport& transport_;

  using Channel =
      boost::asio::experimental::channel<void(boost::system::error_code)>;

  std::shared_ptr<Channel> last_write_;
};

}  // namespace net