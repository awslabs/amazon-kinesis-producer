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

#ifndef AWS_KINESIS_CORE_AGGREGATOR_H_
#define AWS_KINESIS_CORE_AGGREGATOR_H_

#include <memory>
#include <mutex>
#include <vector>

#include <aws/kinesis/core/shard_map.h>
#include <aws/kinesis/core/kinesis_record.h>
#include <aws/kinesis/core/reducer.h>
#include <aws/kinesis/core/configuration.h>
#include <aws/utils/concurrent_hash_map.h>
#include <aws/utils/executor.h>
#include <aws/utils/processing_statistics_logger.h>

namespace aws {
namespace kinesis {
namespace core {

class Aggregator : boost::noncopyable {
 public:
  using DeadlineCallback = std::function<void (std::shared_ptr<KinesisRecord>)>;
  using ReducerMap =
      aws::utils::ConcurrentHashMap<uint64_t,
                                    Reducer<UserRecord, KinesisRecord>>;

  Aggregator(
      const std::shared_ptr<aws::utils::Executor>& executor,
      const std::shared_ptr<ShardMap> shard_map,
      const DeadlineCallback& deadline_callback,
      const std::shared_ptr<aws::kinesis::core::Configuration>& config,
      aws::utils::flush_statistics_aggregator& flush_stats,
      const std::shared_ptr<aws::metrics::MetricsManager>& metrics_manager =
          std::make_shared<aws::metrics::NullMetricsManager>())
      : executor_(executor),
        shard_map_(shard_map),
        deadline_callback_(deadline_callback),
        config_(config),
        flush_stats_(flush_stats),
        metrics_manager_(metrics_manager),
        reducers_([this](auto) { return this->make_reducer(); }) {}

  std::shared_ptr<KinesisRecord> put(const std::shared_ptr<UserRecord>& ur) {
    // If shard map is not available, or aggregation is disabled, just send the
    // record by itself, and do not attempt to aggrgegate.
    boost::optional<uint64_t> shard_id;
    if (config_->aggregation_enabled() && shard_map_) {
      shard_id = shard_map_->shard_id(ur->hash_key());
    }
    if (!shard_id) {
      auto kr = std::make_shared<KinesisRecord>();
      kr->add(ur);
      return kr;
    } else {
      ur->predicted_shard(*shard_id);
      return reducers_[*shard_id].add(ur);
    }
  }

  // TODO unit test for this
  void flush() {
    reducers_.foreach([](auto&, auto v) { v->flush(); });
  }

 private:
  // This cannot be inlined in the lambda because msvc cannot compile that
  Reducer<UserRecord, KinesisRecord>* make_reducer() {
    return new Reducer<UserRecord, KinesisRecord>(
        executor_,
        deadline_callback_,
        config_->aggregation_max_size(),
        config_->aggregation_max_count(),
        flush_stats_
    );
  }

  std::shared_ptr<aws::utils::Executor> executor_;
  std::shared_ptr<ShardMap> shard_map_;
  DeadlineCallback deadline_callback_;
  std::shared_ptr<aws::kinesis::core::Configuration> config_;
  std::shared_ptr<aws::metrics::MetricsManager> metrics_manager_;
  aws::utils::flush_statistics_aggregator& flush_stats_;
  ReducerMap reducers_;
};

} //namespace core
} //namespace kinesis
} //namespace aws

#endif //AWS_KINESIS_CORE_AGGREGATOR_H_
