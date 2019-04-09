/*
 * Copyright 2019 Amazon.com, Inc. or its affiliates.
 * Licensed under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef AWS_UTILS_SPIN_LOCK_H_
#define AWS_UTILS_SPIN_LOCK_H_

#include <atomic>
#include <cstdint>
#include <mutex>
#include <condition_variable>
#include <array>
#include <boost/noncopyable.hpp>

namespace aws {
namespace utils {

class TicketSpinLock : boost::noncopyable {
 public:
  TicketSpinLock()
      : now_serving_(0),
        next_ticket_(0) {}

#ifdef DEBUG
  struct DebugStats {
    std::uint64_t acquired_count;
    std::uint64_t acquired_with_lock;
    std::uint64_t total_spins;
  };

  void add_acquired();
  void add_acquired_lock();
  void add_spin();

  DebugStats get_debug_stats();
#else
#define add_acquired();
#define add_acquired_lock();
#define add_spin();
#endif

  
  inline void lock() noexcept {
    size_t my_ticket = next_ticket_.fetch_add(1);
    std::uint32_t spin_count = 0;
    do {
      add_spin();
      spin_count++;
      if (spin_count > kMaxSpinCount) {
        //
        // Condition variables in C++ are interesting.  They require you take a lock before waiting on the variable.
        // Once you start waiting the lock you originally took is released, which will allow the notifier to pick up
        // the lock on the notification step.
        //
        std::unique_lock<std::mutex> lock(lock_for_ticket(my_ticket));
        //
        // We start waiting on the curent lock holder to notify us that they have completed their work.
        // When the lock holder triggers the notification the provided lambda is used to determine whether this
        // thread should wake up, or instead re-enter the blocked state.  This prevents us from needing to
        // re-enter this blocked state explicitly.
        //
        cv_for_ticket(my_ticket).wait(lock, [this, &my_ticket] { return now_serving_ == my_ticket; });
        spin_count = 0;
        add_acquired_lock();
      }
    } while (now_serving_ != my_ticket);
    add_acquired();
  }

  inline void unlock() noexcept {
    now_serving_++;
    std::unique_lock<std::mutex> lock(lock_for_ticket(now_serving_));
    cv_for_ticket(now_serving_).notify_all();
  }

 private:
  inline std::size_t lock_shard(const std::size_t& ticket) noexcept {
    return ticket % kLockShards;
  }
  inline std::mutex& lock_for_ticket(const std::size_t& ticket) noexcept {
    return condition_locks_[lock_shard(ticket)];
  }
  inline std::condition_variable& cv_for_ticket(const std::size_t& ticket) noexcept {
    return unlocked_cv_[lock_shard(ticket)];
  }
  static const std::uint32_t kMaxSpinCount = 100;

  static const std::size_t kLockShards = 31;
  std::array<std::mutex, kLockShards> condition_locks_;
  std::array<std::condition_variable, kLockShards> unlocked_cv_;
  std::atomic<size_t> now_serving_;
  std::atomic<size_t> next_ticket_;


};

class SpinLock : boost::noncopyable {
 public:

#ifdef DEBUG
  struct DebugStats {
    std::uint64_t acquired_count;
    std::uint64_t acquired_with_lock;
    std::uint64_t total_spins;
  };
  DebugStats get_debug_stats() {
    return DebugStats();
  }
#endif
  inline void lock() noexcept {
    while (!try_lock());
  }

  inline bool try_lock() noexcept {
    return !flag_.test_and_set();
  }

  inline void unlock() noexcept {
    flag_.clear();
  }

 private:
  std::atomic_flag flag_ = ATOMIC_FLAG_INIT;
};

} //namespace utils
} //namespace aws

#endif //AWS_UTILS_SPIN_LOCK_H_
