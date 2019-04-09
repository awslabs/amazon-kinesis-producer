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

#ifndef AWS_UTILS_CONCURRENT_HASH_MAP_H_
#define AWS_UTILS_CONCURRENT_HASH_MAP_H_

#include <functional>
#include <unordered_map>

#include <boost/noncopyable.hpp>

#include <aws/mutex.h>

namespace aws {
namespace utils {

template <typename K, typename V>
class ConcurrentHashMap : boost::noncopyable {
 public:
  using Factory = std::function<V* (const K&)>;

  ConcurrentHashMap(Factory&& factory)
      : factory_(std::forward<Factory>(factory)) {}

  ~ConcurrentHashMap() {
    foreach([](auto& k, auto v) { delete v; });
  }

  V& get(const K& key) {
    auto it = map_.end();

    {
      ReadLock lock(mutex_);
      it = map_.find(key);
    }

    if (it == map_.end()) {
      WriteLock lock(mutex_);
      // Double check that someone didn't get there first
      it = map_.find(key);
      if (it == map_.end()) {
        it = map_.insert(std::make_pair(key, factory_(key))).first;
      }
    }

    return *it->second;
  }

  V& operator [](const K& key) {
    return get(key);
  }

  void foreach(const std::function<void (const K&, V*)>& f) {
    ReadLock lock(mutex_);
    for (auto it = map_.begin(); it != map_.end(); ++it) {
      if (it->second != nullptr) {
        f(it->first, it->second);
      }
    }
  }

 private:
  using Mutex = aws::shared_mutex;
  using ReadLock = aws::shared_lock<Mutex>;
  using WriteLock = aws::unique_lock<Mutex>;

  Factory factory_;
  Mutex mutex_;
  std::unordered_map<K, V*> map_;
};

} //namespace utils
} //namespace aws

#endif //AWS_UTILS_CONCURRENT_HASH_MAP_H_
