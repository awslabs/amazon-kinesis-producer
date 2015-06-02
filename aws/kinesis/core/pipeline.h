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

#include <aws/kinesis/core/aggregator.h>
#include <aws/kinesis/core/collector.h>
#include <aws/kinesis/core/configuration.h>
#include <aws/kinesis/core/ipc_manager.h>
#include <aws/kinesis/core/limiter.h>
#include <aws/kinesis/core/retrier.h>
#include <aws/auth/sigv4.h>
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
      const std::string& stream,
      std::shared_ptr<Configuration> config,
      std::shared_ptr<aws::utils::Executor> executor,
      std::shared_ptr<aws::http::HttpClient> http_client,
      const std::shared_ptr<aws::auth::AwsCredentialsProvider>& creds_provider,
      std::shared_ptr<aws::metrics::MetricsManager> metrics_manager,
      Retrier::UserRecordCallback finish_user_record_cb)
      : region_(std::move(region)),
        config_(std::move(config)),
        executor_(std::move(executor)),
        http_client_(std::move(http_client)),
        metrics_manager_(std::move(metrics_manager)),
        finish_user_record_cb_(std::move(finish_user_record_cb)),
        sig_v4_ctx_(
            std::make_shared<aws::auth::SigV4Context>(
                region_,
                "kinesis",
                creds_provider)),
        shard_map_(
            std::make_shared<ShardMap>(
                executor_,
                http_client_,
                creds_provider,
                region_,
                stream,
                metrics_manager_)),
        aggregator_(
            std::make_shared<Aggregator>(
                executor_,
                shard_map_,
                [this](auto kr) { this->limiter_put(kr); },
                config_,
                metrics_manager_)),
        limiter_(
            std::make_shared<Limiter>(
                executor_,
                [this](auto& kr) { this->collector_put(kr); },
                config_)),
        collector_(
            std::make_shared<Collector>(
                executor_,
                [this](auto prr) { this->send_put_records_request(prr); },
                config_,
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
                .set_stream(stream)
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
    auto request = aws::http::create_kinesis_request(region_,
                                                     "PutRecords",
                                                     prr->serialize());
    try {
      aws::auth::sign_v4(request, *sig_v4_ctx_);
      http_client_->put(
          request,
          [this](auto& result) { this->retrier_put(result); },
          prr,
          prr->deadline());
    } catch (const std::exception& e) {
      retrier_->put(std::make_shared<aws::http::HttpResult>(e.what(), prr));
    }
  }

  void retrier_put(const std::shared_ptr<aws::http::HttpResult>& result) {
    executor_->submit([=] { retrier_->put(result); });
  }

  std::string region_;
  std::shared_ptr<Configuration> config_;
  std::shared_ptr<aws::utils::Executor> executor_;
  std::shared_ptr<aws::http::HttpClient> http_client_;
  std::shared_ptr<aws::metrics::MetricsManager> metrics_manager_;
  Retrier::UserRecordCallback finish_user_record_cb_;

  std::shared_ptr<aws::auth::SigV4Context> sig_v4_ctx_;
  std::shared_ptr<ShardMap> shard_map_;
  std::shared_ptr<Aggregator> aggregator_;
  std::shared_ptr<Limiter> limiter_;
  std::shared_ptr<Collector> collector_;
  std::shared_ptr<Retrier> retrier_;

  std::shared_ptr<aws::metrics::Metric> user_records_rcvd_metric_;
  std::atomic<uint64_t> outstanding_user_records_;
};

} //namespace core
} //namespace kinesis
} //namespace aws

#endif //AWS_KINESIS_CORE_PIPELINE_H_
