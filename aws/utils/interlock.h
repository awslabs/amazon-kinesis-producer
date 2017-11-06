#pragma once

#include <mutex>
#include <condition_variable>

namespace aws {
  namespace utils {
    class Interlock {
    private:
      bool notified_;
      std::mutex mutex_;
      std::condition_variable waiters_;
    public:
      void await();
      void notify();
    };
  }
}