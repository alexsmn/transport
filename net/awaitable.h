#pragma once

#include <boost/asio/awaitable.hpp>

namespace net {

template <class T>
using awaitable = boost::asio::awaitable<T>;

}