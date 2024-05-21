#include "net/write_queue.h"

#include "net/transport.h"

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/use_awaitable.hpp>

namespace net {

void WriteQueue::BlindWrite(std::span<const char> data) {
  boost::asio::co_spawn(
      transport_.GetExecutor(),
      [this, data = std::vector<char>{data.begin(), data.end()}]()
          -> awaitable<void> { auto _ = co_await Write(data); },
      boost::asio::detached);
}

awaitable<ErrorOr<size_t>> WriteQueue::Write(std::span<const char> data) {
  auto current_write = std::make_shared<Channel>(transport_.GetExecutor());

  if (auto last_write = std::exchange(last_write_, current_write)) {
    co_await last_write->async_receive(boost::asio::use_awaitable);
  }

  auto write_result = co_await transport_.Write(data);

  co_await current_write->async_send(boost::system::error_code{},
                                     boost::asio::use_awaitable);

  co_return write_result;
}

}  // namespace net