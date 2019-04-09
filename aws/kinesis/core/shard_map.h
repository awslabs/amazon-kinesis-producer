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

#ifndef AWS_KINESIS_CORE_SHARD_MAP_H_
#define AWS_KINESIS_CORE_SHARD_MAP_H_

#include <boost/multiprecision/cpp_int.hpp>
#include <boost/noncopyable.hpp>
#include <boost/optional.hpp>

#include <aws/kinesis/KinesisClient.h>
#include <aws/metrics/metrics_manager.h>
#include <aws/mutex.h>
#include <aws/utils/utils.h>

namespace aws {
namespace kinesis {
namespace core {

class ShardMap : boost::noncopyable {
 public:
  using uint128_t = boost::multiprecision::uint128_t;
  using TimePoint = std::chrono::steady_clock::time_point;

  ShardMap(std::shared_ptr<aws::utils::Executor> executor,
           std::shared_ptr<Aws::Kinesis::KinesisClient> kinesis_client,
           std::string stream,
           std::shared_ptr<aws::metrics::MetricsManager> metrics_manager
              = std::make_shared<aws::metrics::NullMetricsManager>(),
           std::chrono::milliseconds min_backoff = kMinBackoff,
           std::chrono::milliseconds max_backoff = kMaxBackoff);

  virtual boost::optional<uint64_t> shard_id(const uint128_t& hash_key);

  void invalidate(TimePoint seen_at);

  static uint64_t shard_id_from_str(const std::string& shard_id) {
    auto parts = aws::utils::split_on_first(shard_id, "-");
    return std::stoull(parts.at(1));
  }

  static std::string shard_id_to_str(uint64_t id) {
    auto i = std::to_string(id);
    auto p = std::string(12 - i.length(), '0');
    return "shardId-" + p + i;
  }

 protected:
  ShardMap() {}

 private:
  using Mutex = aws::shared_mutex;
  using ReadLock = aws::shared_lock<Mutex>;
  using WriteLock = aws::unique_lock<Mutex>;

  enum State {
    INVALID,
    UPDATING,
    READY
  };

  static const std::chrono::milliseconds kMinBackoff;
  static const std::chrono::milliseconds kMaxBackoff;

  void update(const std::string& start_shard_id = "");

  void update_callback(
      const Aws::Kinesis::Model::DescribeStreamOutcome& outcome);

  void update_fail(const std::string& code, const std::string& msg = "");

  std::shared_ptr<aws::utils::Executor> executor_;
  std::shared_ptr<Aws::Kinesis::KinesisClient> kinesis_client_;
  std::string stream_;
  std::shared_ptr<aws::metrics::MetricsManager> metrics_manager_;

  State state_;
  std::vector<std::pair<uint128_t, uint64_t>> end_hash_key_to_shard_id_;
  Mutex mutex_;
  TimePoint updated_at_;
  std::chrono::milliseconds min_backoff_;
  std::chrono::milliseconds max_backoff_;
  std::chrono::milliseconds backoff_;
  std::shared_ptr<aws::utils::ScheduledCallback> scheduled_callback_;
};

} //namespace core
} //namespace kinesis
} //namespace aws

#endif //AWS_KINESIS_CORE_SHARD_MAP_H_
