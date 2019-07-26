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

#ifndef AWS_UTILS_CONCURRENT_LINKED_QUEUE_H_
#define AWS_UTILS_CONCURRENT_LINKED_QUEUE_H_

#include <list>

#include <aws/mutex.h>
#include <aws/utils/spin_lock.h>

namespace aws {
namespace utils {

template <typename T>
class ConcurrentLinkedQueue {
 public:
  template <typename U>
  void put(U&& data) {
    Lock lk(mutex_);
    q_.push_back(std::forward<U>(data));
  }

  bool try_take(T& t) {
    Lock lk(mutex_);
    if (q_.empty()) {
      return false;
    } else {
      t = std::move(q_.front());
      q_.pop_front();
      return true;
    }
  }

 private:
  using Mutex = aws::utils::TicketSpinLock;
  using Lock = aws::lock_guard<Mutex>;

  Mutex mutex_;
  std::list<T> q_;
};

} //namespace utils
} //namespace aws

#endif //AWS_UTILS_CONCURRENT_LINKED_QUEUE_H_
