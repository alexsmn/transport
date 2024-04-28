#pragma once

#include "net/awaitable.h"

namespace net {

inline awaitable<void> CoReturnVoid() {
  co_return;
}

template <class T>
inline awaitable<T> CoReturn(T value) {
  co_return value;
}

}  // namespace net
