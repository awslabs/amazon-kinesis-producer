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

#ifndef AWS_KINESIS_CORE_RETRIER_H_
#define AWS_KINESIS_CORE_RETRIER_H_

#include <aws/core/utils/Outcome.h>
#include <aws/kinesis/KinesisClient.h>
#include <aws/kinesis/core/configuration.h>
#include <aws/kinesis/core/put_records_context.h>
#include <aws/kinesis/core/put_records_request.h>
#include <aws/kinesis/core/shard_map.h>
#include <aws/metrics/metrics_manager.h>

namespace aws {
namespace kinesis {
namespace core {

namespace detail {

class MetricsPutter {
 public:
  MetricsPutter(std::shared_ptr<aws::metrics::MetricsManager> metrics_manager,
                std::string stream)
      : metrics_manager_(std::move(metrics_manager)),
        stream_(stream) {}

  MetricsPutter& operator ()(
        std::string name,
        double val,
        boost::optional<uint64_t> shard_id = boost::none,
        boost::optional<std::string> err_code = boost::none);

 private:
  std::shared_ptr<aws::metrics::MetricsManager> metrics_manager_;
  std::string stream_;
};

} // namespace detail

class Retrier {
 public:
  using Configuration = aws::kinesis::core::Configuration;
  using TimePoint = std::chrono::steady_clock::time_point;
 // using Result = std::shared_ptr<aws::http::HttpResult>;
  using UserRecordCallback =
      std::function<void (const std::shared_ptr<UserRecord>&)>;
  using ShardMapInvalidateCallback = std::function<void (TimePoint)>;
  using ErrorCallback =
      std::function<void (const std::string&, const std::string&)>;

  Retrier(std::shared_ptr<Configuration> config,
          UserRecordCallback finish_cb,
          UserRecordCallback retry_cb,
          ShardMapInvalidateCallback shard_map_invalidate_cb,
          ErrorCallback error_cb = ErrorCallback(),
          std::shared_ptr<aws::metrics::MetricsManager> metrics_manager =
              std::make_shared<aws::metrics::NullMetricsManager>())
      : config_(config),
        finish_cb_(finish_cb),
        retry_cb_(retry_cb),
        shard_map_invalidate_cb_(shard_map_invalidate_cb),
        error_cb_(error_cb),
        metrics_manager_(metrics_manager) {}

  void put(std::shared_ptr<PutRecordsContext> prc) {
    handle_put_records_result(std::move(prc));
  }

  void put(const std::shared_ptr<KinesisRecord>& kr,
           const std::string& err_code,
           const std::string& err_msg) {
    auto now = std::chrono::steady_clock::now();
    retry_not_expired(kr, now, now, err_code, err_msg);
  }

 private:
  void handle_put_records_result(std::shared_ptr<PutRecordsContext> prc);

  void retry_not_expired(const std::shared_ptr<KinesisRecord>& kr,
                         TimePoint start,
                         TimePoint end,
                         const std::string& err_code,
                         const std::string& err_msg);

  void retry_not_expired(const std::shared_ptr<UserRecord>& ur,
                         TimePoint start,
                         TimePoint end,
                         const std::string& err_code,
                         const std::string& err_msg);

  void fail(const std::shared_ptr<KinesisRecord>& kr,
            TimePoint start,
            TimePoint end,
            const std::string& err_code,
            const std::string& err_msg);

  void fail(const std::shared_ptr<UserRecord>& ur,
            TimePoint start,
            TimePoint end,
            const std::string& err_code,
            const std::string& err_msg);

  void succeed_if_correct_shard(const std::shared_ptr<UserRecord>& ur,
                                TimePoint start,
                                TimePoint end,
                                const std::string& shard_id,
                                const std::string& sequence_number);

  void finish_user_record(const std::shared_ptr<UserRecord>& ur,
                          const Attempt& final_attempt);

  void emit_metrics(const std::shared_ptr<UserRecord>& ur);

  void emit_metrics(const std::shared_ptr<PutRecordsContext>& prc);

  std::shared_ptr<Configuration> config_;
  UserRecordCallback finish_cb_;
  UserRecordCallback retry_cb_;
  ShardMapInvalidateCallback shard_map_invalidate_cb_;
  ErrorCallback error_cb_;
  std::shared_ptr<aws::metrics::MetricsManager> metrics_manager_;
};

} //namespace core
} //namespace kinesis
} //namespace aws

#endif //AWS_KINESIS_CORE_RETRIER_H_
