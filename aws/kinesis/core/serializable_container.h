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

#ifndef AWS_KINESIS_CORE_SERIALIZABLE_CONTAINER_H_
#define AWS_KINESIS_CORE_SERIALIZABLE_CONTAINER_H_

#include <memory>
#include <stack>
#include <vector>

#include <aws/utils/time_sensitive.h>

namespace aws {
namespace kinesis {
namespace core {

template <typename T>
class SerializableContainer : public aws::utils::TimeSensitive {
 public:
  using TimePoint = std::chrono::steady_clock::time_point;

  virtual ~SerializableContainer() = default;

  virtual void add(const std::shared_ptr<T>& new_item) {
    deadlines_and_expirations_.push(
        std::make_pair<TimePoint, TimePoint>(
            this->deadline(),
            this->expiration()));
    this->inherit_deadline_and_expiration(*new_item);

    items_.push_back(new_item);
    after_add(new_item);
  }

  virtual std::shared_ptr<T> remove_last() {
    if (items_.empty()) {
      return std::shared_ptr<T>();
    }

    auto& prev_times = deadlines_and_expirations_.top();
    set_deadline(prev_times.first);
    set_expiration(prev_times.second);
    deadlines_and_expirations_.pop();

    auto i = std::move(items_.back());
    items_.pop_back();
    after_remove(i);
    return i;
  }

  virtual size_t estimated_size() {
    return accurate_size();
  }

  virtual size_t accurate_size() = 0;

  virtual void clear() {
    set_deadline_from_now(std::chrono::hours(0xFFFFFFFF));
    set_expiration_from_now(std::chrono::hours(0xFFFFFFFF));
    while (!deadlines_and_expirations_.empty()) {
      deadlines_and_expirations_.pop();
    }

    items_.clear();
    after_clear();
  }

  virtual std::string serialize() = 0;

  const std::vector<std::shared_ptr<T>>& items() noexcept {
    return items_;
  }

  size_t size() const noexcept {
    return items_.size();
  }

  bool empty() const noexcept {
    return items_.empty();
  }

 protected:
  virtual void after_add(const std::shared_ptr<T>& new_item) {}
  virtual void after_remove(const std::shared_ptr<T>& item) {}
  virtual void after_clear() {}

  std::vector<std::shared_ptr<T>> items_;
  std::stack<std::pair<TimePoint, TimePoint>> deadlines_and_expirations_;
};

} //namespace core
} //namespace kinesis
} //namespace aws

#endif //AWS_KINESIS_CORE_SERIALIZABLE_CONTAINER_H_
