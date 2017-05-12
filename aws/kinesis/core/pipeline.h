// Copyright 2015 Amazon.com, Inc. or its affiliates. All Rights Reserved.
//
// Licensed under the Amazon Software License (the "License").
// You may not use this file except in compliance with the License.
// A copy of the License is located at
//
//  http://aws.amazon.com/asl
//
// or in the "license" file accompanying this file. This file is distributed
// on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
// express or implied. See the License for the specific language governing
// permissions and limitations under the License.

#ifndef AWS_KINESIS_CORE_PIPELINE_H_
#define AWS_KINESIS_CORE_PIPELINE_H_

#include <boost/format.hpp>
#include <iomanip>

#include <aws/kinesis/core/aggregator.h>
#include <aws/kinesis/core/collector.h>
#include <aws/kinesis/core/configuration.h>
#include <aws/kinesis/core/ipc_manager.h>
#include <aws/kinesis/core/limiter.h>
#include <aws/kinesis/core/put_records_context.h>
#include <aws/kinesis/core/retrier.h>
#include <aws/kinesis/core/FlushStats.h>
#include <aws/kinesis/KinesisClient.h>
#include <aws/metrics/metrics_manager.h>

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
        config_(std::move(config)),
        ur_to_kr_stats_(stream_, "UserRecords", "KinesisRecords"),
        kr_to_put_stats_(stream_, "KinesisRecords", "PutRecords"),
        outstanding_put_reqs_(0),
        started_reqs_(0),
        completed_reqs_(0),
        total_time_(0),
        total_reqs_time_(0),
        report_thread_(std::bind(&Pipeline::report_and_reset, this)),
        executor_(std::move(executor)),
        kinesis_client_(std::move(kinesis_client)),
        metrics_manager_(std::move(metrics_manager)),
        finish_user_record_cb_(std::move(finish_user_record_cb)),
        shard_map_(
            std::make_shared<ShardMap>(
                executor_,
                kinesis_client_,
                stream_,
                metrics_manager_)),
        aggregator_(
            std::make_shared<Aggregator>(
                    executor_,
                    shard_map_,
                    [this](auto kr) { this->limiter_put(kr); },
                    config_,
                    ur_to_kr_stats_,
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
                    kr_to_put_stats_,
                    metrics_manager_)),
        retrier_(
            std::make_shared<Retrier>(
                config_,
                [this](auto& ur) { this->finish_user_record(ur); },
                [this](auto& ur) { this->aggregator_put(ur); },
                [this](TimePoint tp) { shard_map_->invalidate(tp); },
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

  void report_and_reset() {
    while (true) {
      {
        using namespace std::chrono_literals;
        std::this_thread::sleep_for(15s);
      }
      LOG(info) << "Stage 1: " << ur_to_kr_stats_;
      ur_to_kr_stats_.reset();

      LOG(info) << "Stage 2: " << kr_to_put_stats_;
      kr_to_put_stats_.reset();

      LOG(info) << "(" << stream_ << ") Outstanding Put Requests: " << outstanding_put_reqs_;
      LOG(info) << "(" << stream_ << ") { started: " << started_reqs_ << ", completed: " << completed_reqs_ << " }";
      started_reqs_ = 0;
      completed_reqs_ = 0;

      std::uint64_t total_time = total_time_;
      std::uint64_t requests = total_reqs_time_;

      total_time_ = 0;
      total_reqs_time_ = 0;

      double average_req_time = total_time / static_cast<double>(requests);
      double max_buffer_warn_limit = config_->record_max_buffered_time() * 5.0;
      if (average_req_time > max_buffer_warn_limit) {
        LOG(warning) << "Requests are taking more than " << max_buffer_warn_limit << " to complete.  "
                  << "You may need to adjust your configuration to reduce the request time.";
      }
      LOG(info) << "(" << stream_ << ") Average Request Time: " << std::setprecision(8) << average_req_time << " ms";

    }
  }

  void aggregator_put(const std::shared_ptr<UserRecord>& ur) {
    auto kr = aggregator_->put(ur);
    if (kr) {
      limiter_put(kr);
    }
  }

  void limiter_put(const std::shared_ptr<KinesisRecord>& kr) {
    limiter_->put(kr);
  }

  void collector_put(const std::shared_ptr<KinesisRecord>& kr) {
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
    auto prc = std::make_shared<PutRecordsContext>(stream_, prr->items());
    prc->set_start(std::chrono::steady_clock::now());
    ++outstanding_put_reqs_;
    ++started_reqs_;
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
    outstanding_put_reqs_.fetch_sub(1);
    completed_reqs_.fetch_add(1);

    total_time_.fetch_add(context->duration_millis());
    total_reqs_time_.fetch_add(1);

  }

  void retrier_put_kr(const std::shared_ptr<KinesisRecord>& kr) {
    executor_->submit([=] {
      retrier_->put(kr,
                    "Expired",
                    "Expiration reached while waiting in limiter");
    });
  }

  std::string stream_;
  std::string region_;
  std::shared_ptr<Configuration> config_;
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
  FlushStats ur_to_kr_stats_;
  FlushStats kr_to_put_stats_;
  std::thread report_thread_;
  std::atomic<std::uint64_t> outstanding_put_reqs_;
  std::atomic<std::uint64_t> completed_reqs_;
  std::atomic<std::uint64_t> started_reqs_;
  std::atomic<std::uint64_t> total_time_;
  std::atomic<std::uint64_t> total_reqs_time_;
};

} //namespace core
} //namespace kinesis
} //namespace aws

#endif //AWS_KINESIS_CORE_PIPELINE_H_
