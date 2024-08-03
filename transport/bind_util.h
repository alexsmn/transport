#pragma once

#include <functional>
#include <memory>

namespace transport {

template <typename C, typename A>
inline auto BindFrontWeakPtr(C&& callable, std::weak_ptr<A> weak_arg) {
  return [callable = std::forward<C>(callable),
          weak_arg = std::move(weak_arg)](auto&&... args) {
    if (auto arg = weak_arg.lock()) {
      std::invoke(callable, arg.get(), std::forward<decltype(args)>(args)...);
    }
  };
}

}  // namespace transport
