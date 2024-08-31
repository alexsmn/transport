#pragma once

#include "transport/expected.h"

#include <boost/asio/awaitable.hpp>

namespace transport {

template <class T>
using awaitable = boost::asio::awaitable<T>;

template <class T, class C>
inline awaitable<expected<T>> BindCancelation(std::weak_ptr<C> cancelation,
                                              awaitable<expected<T>> aw) {
  if (cancelation.expired()) {
    co_return ERR_ABORTED;
  }

  expected<T> result = co_await std::move(aw);

  if (cancelation.expired()) {
    co_return ERR_ABORTED;
  }

  co_return result;
}

}  // namespace transport