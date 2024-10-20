#pragma once

#include <boost/asio/executor.hpp>
#include <boost/asio/is_executor.hpp>
#include <memory>

namespace transport {

class immediate_executor {
 public:
  immediate_executor() = default;
  ~immediate_executor() = default;

  immediate_executor(const immediate_executor& other) {}
  immediate_executor(immediate_executor&& other) noexcept {}

  bool operator==(const immediate_executor& other) const {
    return this == &other;
  }

  bool operator!=(const immediate_executor& other) const {
    return this != &other;
  }

  void on_work_started() {}
  void on_work_finished() {}

  boost::asio::execution_context& context() { return context_; }

  template <class F, class A>
  void dispatch(F&& f, A&& a) const {
    std::move(f)();
  }

  template <class F, class A>
  void post(F&& f, A&& a) const {
    std::move(f)();
  }

  template <class F, class A>
  void defer(F&& f, A&& a) const {
    std::move(f)();
  }

  boost::asio::execution_context& query(
      boost::asio::execution::context_t) const noexcept {
    return context_;
  }

  static constexpr boost::asio::execution::blocking_t::never_t query(
      boost::asio::execution::blocking_t) noexcept {
    // This executor always has blocking.never semantics.
    return boost::asio::execution::blocking.never;
  }

  template <class F>
  void execute(F&& f) const {
    std::move(f)();
  }

 private:
  mutable boost::asio::execution_context context_;
};

static_assert(boost::asio::is_executor<immediate_executor>::value,
              "immediate_executor must meet the requirements of executor");

}  // namespace transport
