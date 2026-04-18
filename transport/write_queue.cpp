#include "transport/write_queue.h"

#include "transport/any_transport.h"

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/use_awaitable.hpp>

namespace transport {

WriteQueue::WriteQueue(any_transport& transport)
    : state_{std::make_shared<State>(transport)} {}

void WriteQueue::BlindWrite(std::span<const char> data) {
  auto state = state_;
  boost::asio::co_spawn(
      state->transport->get_executor(),
      [state, data = std::vector<char>{data.begin(), data.end()},
       cancelation = std::weak_ptr{state->cancelation}]() -> awaitable<void> {
        if (cancelation.expired()) {
          co_return;
        }
        auto _ = co_await Write(state, data);
      },
      boost::asio::detached);
}

awaitable<expected<size_t>> WriteQueue::Write(std::span<const char> data) {
  co_return co_await Write(state_, data);
}

awaitable<expected<size_t>> WriteQueue::Write(
    const std::shared_ptr<State>& state,
    std::span<const char> data) {
  auto current_write = std::make_shared<Channel>(state->transport->get_executor(),
                                                 /*max_buffer_size =*/1);

  auto cancelation = std::weak_ptr{state->cancelation};

  if (auto last_write = std::exchange(state->last_write, current_write)) {
    co_await last_write->async_receive(boost::asio::use_awaitable);
  }

  if (cancelation.expired()) {
    co_return ERR_ABORTED;
  }

  auto write_result = co_await state->transport->write(data);

  co_await current_write->async_send(boost::system::error_code{},
                                     boost::asio::use_awaitable);

  co_return write_result;
}

}  // namespace transport
