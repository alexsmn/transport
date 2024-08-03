#pragma once

#include "transport/executor.h"

#include <boost/asio/steady_timer.hpp>

namespace transport {

class Timer {
 public:
  using Period = std::chrono::nanoseconds;

  explicit Timer(const Executor& executor) : executor_{executor} {}

  template <class Callback>
  void StartOne(Period period, Callback&& callback) {
    core_ = std::make_shared<CoreImpl<false, Callback>>(
        executor_, period, std::forward<Callback>(callback));
    core_->Start();
  }

  template <class Callback>
  void StartRepeating(Period period, Callback&& callback) {
    core_ = std::make_shared<CoreImpl<true, Callback>>(
        executor_, period, std::forward<Callback>(callback));
    core_->Start();
  }

  void Stop() { core_ = nullptr; }

 private:
  struct Core {
    virtual ~Core() {}
    virtual void Start() = 0;
  };

  template <bool kRepeating, class Callback>
  struct CoreImpl
      : public Core,
        public std::enable_shared_from_this<CoreImpl<kRepeating, Callback>> {
    CoreImpl(const Executor& executor, Period period, Callback&& callback)
        : timer_{executor},
          period_{period},
          callback_{std::forward<Callback>(callback)} {}

    ~CoreImpl() { timer_.cancel(); }

    virtual void Start() override {
      timer_.expires_after(period_);
      timer_.async_wait(
          [weak_core = this->weak_from_this()](boost::system::error_code ec) {
            if (ec == boost::asio::error::operation_aborted)
              return;
            if (auto core = weak_core.lock())
              core->callback_();
            if (kRepeating) {
              if (auto core = weak_core.lock())
                core->Start();
            }
          });
    }

    boost::asio::steady_timer timer_;
    const Period period_;
    Callback callback_;
  };

  Executor executor_;

  std::shared_ptr<Core> core_;
};

}  // namespace transport