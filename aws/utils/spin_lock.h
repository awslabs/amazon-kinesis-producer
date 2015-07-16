// Copyright 2015 Amazon.com, Inc. or its affiliates. All Rights Reserved.
//
// Licensed under the Amazon Software License (the "License").
// You may not use this file except in compliance with the License.
// A copy of the License is located at
//
//  http://aws.amazon.com/asl
//
// or in the "license" file accompanying this file. This file is distributed
// on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
// express or implied. See the License for the specific language governing
// permissions and limitations under the License.

#ifndef AWS_UTILS_SPIN_LOCK_H_
#define AWS_UTILS_SPIN_LOCK_H_

#include <atomic>

#include <boost/noncopyable.hpp>

namespace aws {
namespace utils {

class TicketSpinLock : boost::noncopyable {
 public:
  TicketSpinLock()
      : now_serving_(0),
        next_ticket_(0) {}

  inline void lock() noexcept {
    size_t my_ticket = next_ticket_.fetch_add(1);
    while (now_serving_ != my_ticket);
  }

  inline void unlock() noexcept {
    now_serving_++;
  }

 private:
  std::atomic<size_t> now_serving_;
  std::atomic<size_t> next_ticket_;
};

class SpinLock : boost::noncopyable {
 public:
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
