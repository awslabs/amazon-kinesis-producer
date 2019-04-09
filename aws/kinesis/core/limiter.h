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

#ifndef AWS_KINESIS_CORE_LIMITER_H_
#define AWS_KINESIS_CORE_LIMITER_H_

#include <boost/noncopyable.hpp>
#include <boost/multi_index_container.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/tag.hpp>

#include <aws/kinesis/core/configuration.h>
#include <aws/kinesis/core/kinesis_record.h>
#include <aws/utils/concurrent_hash_map.h>
#include <aws/utils/concurrent_linked_queue.h>
#include <aws/utils/executor.h>
#include <aws/utils/logging.h>
#include <aws/utils/time_sensitive_queue.h>
#include <aws/utils/token_bucket.h>

namespace aws {
namespace kinesis {
namespace core {

namespace detail {

// The limiter in its current form is not going to eliminate throttling errors,
// but it will put a cap on how many can occur per unit time. The number of
// throttling errors will scale linearly with the number of producer instances
// if every instance is putting as fast as it can and there isn't enough
// capacity in the stream to absorb the traffic.
class ShardLimiter : boost::noncopyable {
 public:
  using Callback =
      std::function<void (const std::shared_ptr<KinesisRecord>&)>;

  static const constexpr uint64_t kRecordsPerSecLimit = 1000;
  static const constexpr uint64_t kBytesPerSecLimit = 1024 * 1024;

  ShardLimiter(double token_growth_multiplier = 1.0) {
    token_bucket_.add_token_stream(
          kRecordsPerSecLimit,
          token_growth_multiplier * kRecordsPerSecLimit);
    token_bucket_.add_token_stream(
          kBytesPerSecLimit,
          token_growth_multiplier * kBytesPerSecLimit);
  }

  void put(std::shared_ptr<KinesisRecord> incoming,
           const Callback& callback,
           const Callback& expired_callback) {
    queue_.put(std::move(incoming));
    drain(callback, expired_callback);
  }

  void drain(const Callback& callback, const Callback& expired_callback) {
    if (draining_.test_and_set()) {
      return;
    }

    std::shared_ptr<KinesisRecord> kr;
    while (queue_.try_take(kr)) {
      internal_queue_.insert(std::move(kr));
    }

    internal_queue_.consume_expired(expired_callback);

    internal_queue_.consume_by_deadline([&](const auto& kr) {
      double bytes = kr->accurate_size();
      if (token_bucket_.try_take({1, bytes})) {
        callback(kr);
        return true;
      }
      return false;
    });

    draining_.clear();
  }

 private:
  // The bucket and internal queue are synchronized with the draining_ flag,
  // only one thread can be performing drain at a time.
  std::atomic_flag draining_ = ATOMIC_FLAG_INIT;
  aws::utils::TokenBucket token_bucket_;
  aws::utils::TimeSensitiveQueue<KinesisRecord> internal_queue_;
  aws::utils::ConcurrentLinkedQueue<std::shared_ptr<KinesisRecord>> queue_;
};

} // namespace detail

class Limiter : boost::noncopyable {
 public:
  Limiter(std::shared_ptr<aws::utils::Executor> executor,
          detail::ShardLimiter::Callback callback,
          detail::ShardLimiter::Callback expired_callback,
          std::shared_ptr<Configuration> config)
      : executor_(executor),
        callback_(callback),
        expired_callback_(expired_callback),
        limiters_([=](auto) {
          return new detail::ShardLimiter(
              (double) config->rate_limit() / 100.0);
        }) {
    poll();
  }

  void add_error(const std::string& code, const std::string& msg) {
    // TODO react to throttling errors
  }

  void put(const std::shared_ptr<KinesisRecord>& kr) {
    // Limiter doesn't work if we don't know which shard the record is going to
    auto shard_id = kr->items().front()->predicted_shard();
    if (!shard_id) {
      callback_(kr);
    } else {
      limiters_[*shard_id].put(kr, callback_, expired_callback_);
    }
  }

 private:
  static constexpr const int kDrainDelayMillis = 25;

  void poll() {
    limiters_.foreach([this](auto, auto limiter) {
      limiter->drain(callback_, expired_callback_);
    });

    std::chrono::milliseconds delay(kDrainDelayMillis);
    if (!scheduled_poll_) {
      scheduled_poll_ = executor_->schedule([this] { this->poll(); }, delay);
    } else {
      scheduled_poll_->reschedule(delay);
    }
  }

  std::shared_ptr<aws::utils::Executor> executor_;
  detail::ShardLimiter::Callback callback_;
  detail::ShardLimiter::Callback expired_callback_;
  aws::utils::ConcurrentHashMap<uint64_t, detail::ShardLimiter> limiters_;
  std::shared_ptr<aws::utils::ScheduledCallback> scheduled_poll_;
};

} //namespace core
} //namespace kinesis
} //namespace aws

#endif //AWS_KINESIS_CORE_LIMITER_H_
