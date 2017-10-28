#pragma once

#include <boost/asio/io_service.hpp>
#include <boost/asio/steady_timer.hpp>

namespace net {

class Timer {
 public:
  using Period = std::chrono::nanoseconds;

  Timer(boost::asio::io_service& io_service)
      : io_service_{io_service} {
  }

  template<class Callback>
  void StartOne(Period period, Callback&& callback) {
    core_ = std::make_shared<CoreImpl<false, Callback>>(io_service_, period, std::forward<Callback>(callback));
    core_->Start();
  }

  template<class Callback>
  void StartRepeating(Period period, Callback&& callback) {
    core_ = std::make_shared<CoreImpl<true, Callback>>(io_service_, period, std::forward<Callback>(callback));
    core_->Start();
  }

  void Stop() {
    core_ = nullptr;
  }

 private:
  struct Core {
    virtual ~Core() {}
    virtual void Start() = 0;
  };

  template<bool kRepeating, class Callback>
  struct CoreImpl : public Core,
                    public std::enable_shared_from_this<CoreImpl<kRepeating, Callback>> {
    CoreImpl(boost::asio::io_service& io_service, Period period, Callback&& callback)
        : timer_{io_service},
          period_{period},
          callback_{std::forward<Callback>(callback)} {
    }

    ~CoreImpl() {
      timer_.cancel();
    }

    virtual void Start() override {
      auto weak_core = std::weak_ptr<CoreImpl<kRepeating, Callback>>(shared_from_this());
      timer_.expires_from_now(period_);
      timer_.async_wait([weak_core](boost::system::error_code ec) {
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

  boost::asio::io_service& io_service_;
  std::shared_ptr<Core> core_;
};

} // namespace net