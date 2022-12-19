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

#ifndef AWS_KINESIS_CORE_PIPELINE_H_
#define AWS_KINESIS_CORE_PIPELINE_H_

#include <boost/format.hpp>
#include <iomanip>

#include <aws/core/utils/ARN.h>
#include <aws/core/utils/StringUtils.h>
#include <aws/kinesis/core/aggregator.h>
#include <aws/kinesis/core/collector.h>
#include <aws/kinesis/core/configuration.h>
#include <aws/kinesis/core/ipc_manager.h>
#include <aws/kinesis/core/limiter.h>
#include <aws/kinesis/core/put_records_context.h>
#include <aws/kinesis/core/retrier.h>
#include <aws/kinesis/KinesisClient.h>
#include <aws/metrics/metrics_manager.h>
#include <aws/utils/processing_statistics_logger.h>
#include <aws/sts/STSClient.h>
#include <aws/sts/model/GetCallerIdentityRequest.h>
#include <aws/sts/model/GetCallerIdentityResult.h>

#include <aws/utils/logging.h>


namespace aws {
namespace kinesis {
namespace core {

class Pipeline : boost::noncopyable {
 public:
  using Configuration = aws::kinesis::core::Configuration;
  using TimePoint = std::chrono::steady_clock::time_point;

  Pipeline(
      std::string region,
      std::string stream,
      std::shared_ptr<Configuration> config,
      std::shared_ptr<aws::utils::Executor> executor,
      std::shared_ptr<Aws::Kinesis::KinesisClient> kinesis_client,
      std::shared_ptr<aws::metrics::MetricsManager> metrics_manager,
      Retrier::UserRecordCallback finish_user_record_cb)
      : stream_(std::move(stream)),
        region_(std::move(region)),
        stream_arn_(std::move(init_stream_arn(region_, stream_))),
        config_(std::move(config)),
        stats_logger_(stream_, config_->record_max_buffered_time()),
        executor_(std::move(executor)),
        kinesis_client_(std::move(kinesis_client)),
        metrics_manager_(std::move(metrics_manager)),
        finish_user_record_cb_(std::move(finish_user_record_cb)),
        shard_map_(
            std::make_shared<ShardMap>(
                executor_,
                kinesis_client_,
                stream_,
                stream_arn_,
                metrics_manager_)),
        aggregator_(
            std::make_shared<Aggregator>(
                    executor_,
                    shard_map_,
                    [this](auto kr) { this->limiter_put(kr); },
                    config_,
                    stats_logger_.stage1(),
                    metrics_manager_)),
        limiter_(
            std::make_shared<Limiter>(
                executor_,
                [this](auto& kr) { this->collector_put(kr); },
                [this](auto& kr) { this->retrier_put_kr(kr); },
                config_)),
        collector_(
            std::make_shared<Collector>(
                    executor_,
                    [this](auto prr) { this->send_put_records_request(prr); },
                    config_,
                    stats_logger_.stage2(),
                    metrics_manager_)),
        retrier_(
            std::make_shared<Retrier>(
                config_,
                [this](auto& ur) { this->finish_user_record(ur); },
                [this](auto& ur) { this->aggregator_put(ur); },
                [this](auto& tp, auto predicted_shard) { shard_map_->invalidate(tp, predicted_shard); },
                [this](auto& code, auto& msg) {
                  limiter_->add_error(code, msg);
                },
                metrics_manager_)),
        user_records_rcvd_metric_(
            metrics_manager_
                ->finder()
                .set_name(aws::metrics::constants::Names::UserRecordsReceived)
                .set_stream(stream_)
                .find()),
        outstanding_user_records_(0) {}

  void put(const std::shared_ptr<UserRecord>& ur) {
    outstanding_user_records_++;
    user_records_rcvd_metric_->put(1);
    aggregator_put(ur);
  }

  void flush() {
    aggregator_->flush();
    executor_->schedule(
        [this] { collector_->flush(); },
        std::chrono::milliseconds(80));
  }

  uint64_t outstanding_user_records() const noexcept {
    return outstanding_user_records_;
  }

 private:

  void aggregator_put(const std::shared_ptr<UserRecord>& ur) {
    auto kr = aggregator_->put(ur);
    if (kr) {
      limiter_put(kr);
    }
  }

  void limiter_put(const std::shared_ptr<KinesisRecord>& kr) {
    limiter_->put(kr);
  }

  uint64_t putrecords_buffer_duration() const noexcept {
    return std::min(max_putrecords_buffer_time,
        (uint64_t)(config_->record_max_buffered_time() * putrecords_buffer_ratio));
  }

  void collector_put(const std::shared_ptr<KinesisRecord>& kr) {
    if (config_->aggregation_enabled()) {
      kr->extend_deadline_from_now(std::chrono::milliseconds(putrecords_buffer_duration()));
    }
    auto prr = collector_->put(kr);
    if (prr) {
      send_put_records_request(prr);
    }
  }

  void finish_user_record(const std::shared_ptr<UserRecord>& ur) {
    finish_user_record_cb_(ur);
    outstanding_user_records_--;
  }

  void send_put_records_request(const std::shared_ptr<PutRecordsRequest>& prr) {
    auto prc = std::make_shared<PutRecordsContext>(stream_, stream_arn_, prr->items());
    prc->set_start(std::chrono::steady_clock::now());
    kinesis_client_->PutRecordsAsync(
        prc->to_sdk_request(),
        [this](auto /*client*/,
               auto& /*sdk_req*/,
               auto& outcome,
               auto sdk_ctx) {
          auto ctx = std::dynamic_pointer_cast<PutRecordsContext>(
              std::const_pointer_cast<Aws::Client::AsyncCallerContext>(
                  sdk_ctx));
          ctx->set_end(std::chrono::steady_clock::now());
          ctx->set_outcome(outcome);
          this->request_completed(ctx);
          // At the time of writing, the SDK can spawn a large number of
          // threads in order to achieve request parallelism. These threads will
          // later put items into the IPC manager after they finish the logic in
          // the retrier. This can overwhelm the queue in the IPC manager, which
          // is guarded by a no-backoff spin lock and never intended for
          // use under high contention. To workaround this, we sumbit a task
          // into the pipeline's executor instead. This limits the contention on
          // the IPC manager's queue to the size of the executor's thread pool.
          this->executor_->submit([=] { this->retrier_->put(ctx); });
        },
        prc);
  }

  void request_completed(std::shared_ptr<PutRecordsContext> context) {
    stats_logger_.request_complete(context);
  }

  void retrier_put_kr(const std::shared_ptr<KinesisRecord>& kr) {
    executor_->submit([=] {
      retrier_->put(kr,
                    "Expired",
                    "Expiration reached while waiting in limiter");
    });
  }

  // Retrieve the account ID and partition from the STS service.
  static std::string init_stream_arn(const std::string &region, const std::string &stream_name) {
    Aws::STS::STSClient sts;
    Aws::STS::Model::GetCallerIdentityRequest request;
    auto outcome = sts.GetCallerIdentity(request);
    if (outcome.IsSuccess()) {
      auto result = outcome.GetResult();
      Aws::Utils::ARN sts_arn(result.GetArn());

      // Construct and return the Kinesis stream ARN.
      std::stringstream arn;
      arn << "arn:" << sts_arn.GetPartition() << ":kinesis:" << region << ":" << result.GetAccount()
          << ":stream/" << stream_name;

      auto arn_str = arn.str();
      LOG(info) << "StreamARN \"" << arn_str << "\" has been successfully configured, "
                << "and will be used in requests including ListShards and PutRecords";
      return arn_str;
    }

    LOG(warning) << "Failed to get StreamARN using STS GetCallerIdentity with exception: "
              << outcome.GetError().GetMessage().c_str();
    return {};
  }

  std::string region_;
  std::string stream_;
  std::string stream_arn_;
  std::shared_ptr<Configuration> config_;
  aws::utils::processing_statistics_logger stats_logger_;
  std::shared_ptr<aws::utils::Executor> executor_;
  std::shared_ptr<Aws::Kinesis::KinesisClient> kinesis_client_;
  std::shared_ptr<aws::metrics::MetricsManager> metrics_manager_;
  Retrier::UserRecordCallback finish_user_record_cb_;

  std::shared_ptr<ShardMap> shard_map_;
  std::shared_ptr<Aggregator> aggregator_;
  std::shared_ptr<Limiter> limiter_;
  std::shared_ptr<Collector> collector_;
  std::shared_ptr<Retrier> retrier_;

  std::shared_ptr<aws::metrics::Metric> user_records_rcvd_metric_;
  std::atomic<uint64_t> outstanding_user_records_;
  const float putrecords_buffer_ratio = 0.2;
  const uint64_t max_putrecords_buffer_time = 50;


};

} //namespace core
} //namespace kinesis
} //namespace aws

#endif //AWS_KINESIS_CORE_PIPELINE_H_
