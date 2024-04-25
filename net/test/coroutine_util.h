#pragma once

#include <boost/asio/awaitable.hpp>

namespace net {

inline boost::asio::awaitable<void> CoReturnVoid() {
  co_return;
}

template <class T>
inline boost::asio::awaitable<T> CoReturn(T value) {
  co_return value;
}

}  // namespace net
