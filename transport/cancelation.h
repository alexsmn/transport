#pragma once

#include <memory>

namespace transport {

class cancelation;

class cancelation_state {
 public:
  cancelation_state(const cancelation_state&) = default;
  cancelation_state& operator=(const cancelation_state&) = default;

  cancelation_state(cancelation_state&&) = default;
  cancelation_state& operator=(cancelation_state&&) = default;

  bool canceled() const { return weak_ptr_.expired(); }

 private:
  explicit cancelation_state(std::weak_ptr<void> weak_ptr)
      : weak_ptr_{std::move(weak_ptr)} {}

  std::weak_ptr<void> weak_ptr_;

  friend class cancelation;
};

class cancelation {
 public:
  cancelation() = default;

  cancelation(const cancelation&) = delete;
  cancelation& operator=(const cancelation&) = delete;

  void cancel() { shared_ptr_ = std::make_shared<bool>(); }

  cancelation_state get_state() const { return cancelation_state{shared_ptr_}; }

 private:
  std::shared_ptr<void> shared_ptr_ = std::make_shared<bool>();
};

}  // namespace transport