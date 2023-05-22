#pragma once

#include <promise.hpp/promise.hpp>

namespace net {

template <class T>
using promise = promise_hpp::promise<T>;

}
