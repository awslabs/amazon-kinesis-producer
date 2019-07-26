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

#ifndef AWS_UTILS_TIME_SENSITIVE_QUEUE_H_
#define AWS_UTILS_TIME_SENSITIVE_QUEUE_H_

#include <boost/multi_index_container.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/tag.hpp>

namespace aws {
namespace utils {

template <typename T>
class TimeSensitiveQueue {
 public:
  void consume_by_deadline(
      const std::function<bool (const std::shared_ptr<T>&)>& f) {
    consume<Deadline>(f);
  }

  void consume_expired(
      const std::function<void (const std::shared_ptr<T>&)>& f) {
    consume<Expiration>([&](const auto& p) {
      if (p->expired()) {
        f(p);
        return true;
      }
      return false;
    });
  }

  template <typename U>
  void insert(U&& item) {
    container_.insert(std::forward<U>(item));
  }

  size_t size() const {
    return container_.size();
  }

 private:
  // Index tags
  struct Deadline {};
  struct Expiration {};

  struct GetDeadline {
    using result_type = decltype(((T*) nullptr)->deadline());
    result_type operator ()(const std::shared_ptr<T>& t) {
      return t->deadline();
    }
  };

  struct GetExpiration {
    using result_type = decltype(((T*) nullptr)->expiration());
    result_type operator ()(const std::shared_ptr<T>& t) {
      return t->expiration();
    }
  };

  template <typename Index>
  void consume(const std::function<bool (const std::shared_ptr<T>&)>& f) {
    auto& idx = container_.template get<Index>();
    for (auto it = idx.begin(); it != idx.end() && f(*it); it = idx.erase(it));
  }

  boost::multi_index::multi_index_container<
      std::shared_ptr<T>,
      boost::multi_index::indexed_by<
          boost::multi_index::ordered_non_unique<
              boost::multi_index::tag<Deadline>,
              GetDeadline>,
          boost::multi_index::ordered_non_unique<
              boost::multi_index::tag<Expiration>,
              GetExpiration>>> container_;
};

} //namespace utils
} //namespace aws

#endif //AWS_UTILS_TIME_SENSITIVE_QUEUE_H_
