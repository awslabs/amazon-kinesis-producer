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
#include <boost/optional/optional_io.hpp>

#include <aws/kinesis/KinesisClient.h>
#include <aws/kinesis/model/Shard.h>
#include <aws/metrics/metrics_manager.h>
#include <aws/mutex.h>
#include <aws/utils/utils.h>
#include <thread>

namespace aws {
namespace kinesis {
namespace core {

class ShardMap : boost::noncopyable {
 public:
  using uint128_t = boost::multiprecision::uint128_t;
  using TimePoint = std::chrono::steady_clock::time_point;

  using ListShardsCallBack = std::function<void (
    const Aws::Kinesis::Model::ListShardsRequest& req, 
    const Aws::Kinesis::ListShardsResponseReceivedHandler& handler, 
    const std::shared_ptr<const Aws::Client::AsyncCallerContext>& context)>;

  ShardMap(std::shared_ptr<aws::utils::Executor> executor,
           ListShardsCallBack list_shards_callback,
           std::string stream,
           std::string stream_arn,
           std::shared_ptr<aws::metrics::MetricsManager> metrics_manager
              = std::make_shared<aws::metrics::NullMetricsManager>(),
           std::chrono::milliseconds min_backoff = kMinBackoff,
           std::chrono::milliseconds max_backoff = kMaxBackoff,
           std::chrono::milliseconds closed_shard_ttl = kClosedShardTtl);

  virtual boost::optional<uint64_t> shard_id(const uint128_t& hash_key);
  boost::optional<std::pair<ShardMap::uint128_t, ShardMap::uint128_t>> hashrange(const uint64_t& shard_id);

  void invalidate(const TimePoint& seen_at, const boost::optional<uint64_t> predicted_shard);

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
  static const std::chrono::milliseconds kClosedShardTtl;

  void update();
  void list_shards(const std::string& next_token = "");
  void list_shards_callback(const Aws::Kinesis::Model::ListShardsOutcome& outcome);

  void update_fail(const std::string& code, const std::string& msg = "");

  void clear_all_stored_shards();
  void sort_all_open_shards();
  void cleanup();

  std::shared_ptr<aws::utils::Executor> executor_;
  std::string stream_;
  std::string stream_arn_;
  std::shared_ptr<aws::metrics::MetricsManager> metrics_manager_;
  State state_;
  std::vector<std::pair<uint128_t, uint64_t>> end_hash_key_to_shard_id_;
  std::unordered_map<uint64_t, Aws::Kinesis::Model::Shard> open_shards_;
  std::unordered_map<uint64_t, std::pair<uint128_t, uint128_t>> shard_id_to_shard_hashkey_cache_;
  
  Mutex mutex_;
  Mutex shard_cache_mutex_;
  TimePoint updated_at_;
  std::chrono::milliseconds min_backoff_;
  std::chrono::milliseconds max_backoff_;
  std::chrono::milliseconds backoff_;
  std::chrono::milliseconds closed_shard_ttl_;
  std::shared_ptr<aws::utils::ScheduledCallback> scheduled_callback_;
  ListShardsCallBack list_shards_callback_;
};

} //namespace core
} //namespace kinesis
} //namespace aws

#endif //AWS_KINESIS_CORE_SHARD_MAP_H_
