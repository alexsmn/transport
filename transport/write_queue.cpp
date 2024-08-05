#include "transport/write_queue.h"

#include "transport/any_transport.h"

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/use_awaitable.hpp>

namespace transport {

void WriteQueue::BlindWrite(std::span<const char> data) {
  boost::asio::co_spawn(
      transport_.get_executor(),
      [this, data = std::vector<char>{data.begin(), data.end()},
       cancelation = std::weak_ptr{cancelation_}]() -> awaitable<void> {
        if (cancelation.expired()) {
          co_return;
        }
        auto _ = co_await Write(data);
      },
      boost::asio::detached);
}

awaitable<ErrorOr<size_t>> WriteQueue::Write(std::span<const char> data) {
  auto current_write = std::make_shared<Channel>(transport_.get_executor(),
                                                 /*max_buffer_size =*/1);

  auto cancelation = std::weak_ptr{cancelation_};

  if (auto last_write = std::exchange(last_write_, current_write)) {
    co_await last_write->async_receive(boost::asio::use_awaitable);
  }

  if (cancelation.expired()) {
    co_return ERR_ABORTED;
  }

  auto write_result = co_await transport_.write(data);

  co_await current_write->async_send(boost::system::error_code{},
                                     boost::asio::use_awaitable);

  co_return write_result;
}

}  // namespace transport