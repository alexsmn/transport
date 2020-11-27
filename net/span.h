#pragma once

#include <cstddef>
#include <iterator>

namespace net {

template <class T>
class span {
 public:
  constexpr span() noexcept {}

  constexpr span(T* begin, T* end) noexcept : begin_{begin}, end_{end} {}

  constexpr span(T* data, std::size_t size) noexcept
      : begin_{data}, end_{data + size} {}

  template <class Container>
  constexpr span(const Container& c) noexcept
      : span(std::data(c), std::data(c) + std::size(c)) {}

  template <class U>
  constexpr span(span<U> source) noexcept
      : begin_{source.begin()}, end_{source.end()} {}

  constexpr T& operator[](std::size_t index) const noexcept {
    return begin_[index];
  }

  constexpr std::size_t size() const noexcept { return end_ - begin_; }
  constexpr bool empty() const noexcept { return begin_ == end_; }

  constexpr const T* data() const noexcept { return begin_; }
  constexpr T* data() noexcept { return begin_; }

  constexpr T& front() const noexcept { return *begin_; }
  constexpr T& back() const noexcept { return *(end_ - 1); }

  constexpr T* begin() const noexcept { return begin_; }
  constexpr T* end() const noexcept { return end_; }

  constexpr span subspan(std::size_t offset) const noexcept {
    return span{&begin_[offset], end_};
  }

  constexpr span subspan(std::size_t offset, std::size_t count) const noexcept {
    return span{&begin_[offset], count};
  }

 private:
  T* begin_ = nullptr;
  T* end_ = nullptr;
};

}  // namespace net