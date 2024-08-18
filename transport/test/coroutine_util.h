#pragma once

#include "transport/awaitable.h"

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/io_context.hpp>
#include <gmock/gmock.h>

#define NET_ASSERT_OK(x) ASSERT_EQ(x, ::transport::OK)
#define NET_EXPECT_OK(x) EXPECT_EQ(x, ::transport::OK)

namespace transport {

inline awaitable<void> CoVoid() {
  co_return;
}

template <class T>
inline awaitable<T> CoValue(T value) {
  co_return value;
}

ACTION_P(CoReturn, value) {
  return CoValue(value);
}

ACTION(CoReturnVoid) {
  return CoVoid();
}

template <class F>
void CoTest(F&& func) {
  bool passed = false;

  boost::asio::io_context io_context;

  boost::asio::co_spawn(
      io_context,
      [&]() -> awaitable<void> {
        co_await func();
        passed = true;
      },
      boost::asio::detached);

  io_context.run();

  EXPECT_TRUE(passed);
}

}  // namespace transport
