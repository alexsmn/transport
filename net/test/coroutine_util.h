#pragma once

#include "net/awaitable.h"

#include <gmock/gmock.h>

namespace net {

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

}  // namespace net
