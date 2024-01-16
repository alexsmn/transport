#pragma once

#include <boost/asio/executor.hpp>
#include <memory>

namespace net {

class ImmediateExecutor {
 public:
  ImmediateExecutor() = default;
  ~ImmediateExecutor() = default;

  ImmediateExecutor(const ImmediateExecutor& other) {}
  ImmediateExecutor(ImmediateExecutor&& other) noexcept {}

  bool operator==(const ImmediateExecutor& other) const { return true; }

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

 private:
  boost::asio::execution_context context_;
};

}  // namespace net
