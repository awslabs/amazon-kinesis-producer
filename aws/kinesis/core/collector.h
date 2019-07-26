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

#ifndef AWS_KINESIS_CORE_COLLECTOR_H_
#define AWS_KINESIS_CORE_COLLECTOR_H_

#include <aws/kinesis/core/put_records_request.h>
#include <aws/kinesis/core/reducer.h>
#include <aws/kinesis/core/configuration.h>
#include <aws/utils/concurrent_hash_map.h>
#include <aws/utils/processing_statistics_logger.h>

namespace aws {
namespace kinesis {
namespace core {

class Collector : boost::noncopyable {
 public:
  using FlushCallback =
      std::function<void (std::shared_ptr<PutRecordsRequest>)>;

  Collector(
      const std::shared_ptr<aws::utils::Executor>& executor,
      const FlushCallback& flush_callback,
      const std::shared_ptr<aws::kinesis::core::Configuration>& config,
      aws::utils::flush_statistics_aggregator& flush_stats,
      const std::shared_ptr<aws::metrics::MetricsManager>& metrics_manager =
          std::make_shared<aws::metrics::NullMetricsManager>())
      : flush_callback_(flush_callback),
        reducer_(executor,
                 [this](auto prr) { this->handle_flush(std::move(prr)); },
                 config->collection_max_size(),
                 config->collection_max_count(),
                 flush_stats,
                 [this](auto kr) { return this->should_flush(kr); }),
        buffered_data_([](auto) { return new std::atomic<size_t>(0); }) {}

  std::shared_ptr<PutRecordsRequest>
  put(const std::shared_ptr<KinesisRecord>& kr) {
    auto prr = reducer_.add(kr);
    decrease_buffered_data(prr);
    return prr;
  }

  void flush() {
    reducer_.flush();
  }

 private:
  // We don't want any individual shard to accumulate too much data
  // because that makes traffic to that shard bursty, and might cause
  // throttling, so we flush whenever a shard reaches a certain limit.
  bool should_flush(const std::shared_ptr<KinesisRecord> kr) {
    auto shard_id = kr->items().front()->predicted_shard();
    if (shard_id) {
      auto d = buffered_data_[*shard_id] += kr->accurate_size();
      if (d >= 256 * 1024) {
        return true;
      }
    }
    return false;
  }

  void decrease_buffered_data(const std::shared_ptr<PutRecordsRequest>& prr) {
    if (!prr) {
      return;
    }

    for (auto& kr : prr->items()) {
      auto shard_id = kr->items().front()->predicted_shard();
      if (shard_id) {
        buffered_data_[*shard_id] -= kr->accurate_size();
      }
    }
  }

  void handle_flush(std::shared_ptr<PutRecordsRequest> prr) {
    decrease_buffered_data(prr);
    flush_callback_(std::move(prr));
  }

  FlushCallback flush_callback_;
  std::shared_ptr<aws::metrics::MetricsManager> metrics_manager_;
  Reducer<KinesisRecord, PutRecordsRequest> reducer_;
  aws::utils::ConcurrentHashMap<uint64_t, std::atomic<size_t>> buffered_data_;
};

} //namespace core
} //namespace kinesis
} //namespace aws

#endif //AWS_KINESIS_CORE_COLLECTOR_H_
