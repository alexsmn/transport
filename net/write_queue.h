#pragma once

#include "net/any_transport.h"
#include "net/awaitable.h"
#include "net/error_or.h"

#include <boost/asio/experimental/channel.hpp>
#include <memory>
#include <span>

namespace net {

class Transport;

class WriteQueue {
 public:
  explicit WriteQueue(any_transport& transport)
      : transport_{*transport.get_impl()} {}

  explicit WriteQueue(Transport& transport) : transport_{transport} {}

  ~WriteQueue() {}

  void BlindWrite(std::span<const char> data);

  awaitable<ErrorOr<size_t>> Write(std::span<const char> data);

 private:
  Transport& transport_;

  using Channel =
      boost::asio::experimental::channel<void(boost::system::error_code)>;

  std::shared_ptr<Channel> last_write_;

  std::shared_ptr<bool> cancelation_ = std::make_shared<bool>();
};

}  // namespace net