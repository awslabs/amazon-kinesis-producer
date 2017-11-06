#include "interlock.h"

namespace aws {
  namespace utils {

    void Interlock::notify() {
      std::lock_guard<std::mutex> lock(mutex_);
      notified_ = true;
      waiters_.notify_all();
    }

    void Interlock::await() {
      {
        std::unique_lock<std::mutex> lock(mutex_);
        waiters_.wait(lock, [this]() { return notified_; });
      }

    }


  }
}


