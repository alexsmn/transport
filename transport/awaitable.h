#pragma once

#include "transport/error_or.h"

#include <boost/asio/awaitable.hpp>

namespace transport {

template <class T>
using awaitable = boost::asio::awaitable<T>;

template <class T, class C>
inline awaitable<ErrorOr<T>> BindCancelation(std::weak_ptr<C> cancelation,
                                             awaitable<ErrorOr<T>> aw) {
  if (cancelation.expired()) {
    co_return ERR_ABORTED;
  }

  ErrorOr<T> result = co_await std::move(aw);

  if (cancelation.expired()) {
    co_return ERR_ABORTED;
  }

  co_return result;
}

}  // namespace transport