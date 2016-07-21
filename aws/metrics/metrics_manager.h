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

#ifndef AWS_METRICS_METRICS_MANAGER_H_
#define AWS_METRICS_METRICS_MANAGER_H_

#include <map>
#include <tuple>

#include <boost/date_time/posix_time/posix_time.hpp>

#include <aws/metrics/metric.h>
#include <aws/utils/executor.h>
#include <aws/utils/utils.h>
#include <aws/http/http_client.h>
#include <aws/metrics/metrics_constants.h>
#include <aws/auth/credentials.h>
#include <aws/auth/sigv4.h>
#include <aws/metrics/metrics_finder.h>
#include <aws/metrics/metrics_index.h>

namespace aws {
namespace metrics {

class MetricsManager;

namespace detail {

using Dimension = std::pair<std::string, std::string>;
using Dimensions = std::list<Dimension>;
using ExtraDimensions =
    std::list<std::tuple<std::string, std::string, constants::Granularity>>;
using ExtraDimMap =
    std::map<
        constants::Granularity,
        std::vector<std::pair<std::string, std::string>>>;

class MetricsFinderBuilder {
 public:
  MetricsFinderBuilder(MetricsManager& manager,
                       const ExtraDimMap& extra_dims)
      : state_(EMPTY),
        manager_(manager),
        extra_dims_(extra_dims) {}

  MetricsFinderBuilder& set_name(std::string name) {
    assert(state_ == EMPTY);
    state_ = HAS_NAME;
    mf_.push_dimension(constants::DimensionNames::MetricName, name);
    for (auto& p : extra_dims_.at(constants::Granularity::Global)) {
      mf_.push_dimension(p.first, p.second);
    }
    return *this;
  }

  MetricsFinderBuilder& set_stream(std::string stream) {
    assert(state_ == HAS_NAME || state_ == HAS_ERR_CODE);
    state_ = HAS_STREAM;
    mf_.push_dimension(constants::DimensionNames::StreamName, stream);
    for (auto& p : extra_dims_.at(constants::Granularity::Stream)) {
      mf_.push_dimension(p.first, p.second);
    }
    return *this;
  }

  MetricsFinderBuilder& set_shard(std::string shard) {
    assert(state_ == HAS_STREAM);
    state_ = HAS_SHARD;
    mf_.push_dimension(constants::DimensionNames::ShardId, shard);
    for (auto& p : extra_dims_.at(constants::Granularity::Shard)) {
      mf_.push_dimension(p.first, p.second);
    }
    return *this;
  }

  MetricsFinderBuilder& set_error_code(std::string error_code) {
    assert(state_ == HAS_NAME);
    state_ = HAS_ERR_CODE;
    mf_.push_dimension(constants::DimensionNames::ErrorCode, error_code);
    return *this;
  }

  std::shared_ptr<Metric> find();

 private:
  enum State {
    EMPTY,
    HAS_NAME,
    HAS_ERR_CODE,
    HAS_STREAM,
    HAS_SHARD
  };

  State state_;
  MetricsManager& manager_;
  const ExtraDimMap& extra_dims_;
  MetricsFinder mf_;
};

} //namespace detail

class MetricsManager {
 public:
  MetricsManager(
      std::shared_ptr<aws::utils::Executor> executor,
      std::shared_ptr<aws::http::SocketFactory> socket_factory,
      std::shared_ptr<aws::auth::AwsCredentialsProvider> creds,
      std::string region,
      std::string cw_namespace = "KinesisProducerLib",
      constants::Level level = constants::Level::Detailed,
      constants::Granularity granularity = constants::Granularity::Shard,
      const detail::ExtraDimensions& extra_dimensions =
          detail::ExtraDimensions(),
      std::string custom_endpoint = "",
      int port = 443,
      std::chrono::milliseconds upload_frequency = std::chrono::minutes(1),
      std::chrono::milliseconds retry_frequency = std::chrono::seconds(10))
      : executor_(std::move(executor)),
        endpoint_(custom_endpoint.empty()
                      ? "monitoring." + region + ".amazonaws.com"
                      : custom_endpoint.replace(0,7,"monitoring")),
        http_client_(
            std::make_shared<aws::http::HttpClient>(
                executor_,
                std::move(socket_factory),
                endpoint_,
                port,
                true,
                custom_endpoint.empty())),
        creds_(std::move(creds)),
        region_(std::move(region)),
        cw_namespace_(std::move(cw_namespace)),
        level_(level),
        granularity_(granularity),
        upload_frequency_(upload_frequency),
        retry_frequency_(retry_frequency) {
    if (level_ != constants::Level::None) {
      LOG(info) << "Uploading metrics to " << endpoint_ << ":" << port;
    }

    extra_dimensions_[constants::Granularity::Global];
    extra_dimensions_[constants::Granularity::Stream];
    extra_dimensions_[constants::Granularity::Shard];

    for (auto& t : extra_dimensions) {
      extra_dimensions_[std::get<2>(t)].emplace_back(
          std::move(std::get<0>(t)),
          std::move(std::get<1>(t)));
    }

    scheduled_upload_ =
        executor_->schedule(
            [this] {
              scheduled_upload_->reschedule(upload_frequency_);
              this->upload();
            },
            upload_frequency_);

    scheduled_retry_ =
        executor_->schedule(
            [=] {
              scheduled_retry_->reschedule(retry_frequency_);
              this->retry_uploads();
            },
            retry_frequency_);
  }

  virtual detail::MetricsFinderBuilder finder() {
    return detail::MetricsFinderBuilder(*this, extra_dimensions_);
  }

  virtual std::shared_ptr<Metric> get_metric(const MetricsFinder& finder) {
    return metrics_index_.get_metric(finder);
  }

  virtual std::vector<std::shared_ptr<Metric>> all_metrics() noexcept {
    return metrics_index_.get_all();
  }

  virtual void stop() {
    scheduled_upload_->cancel();
    scheduled_retry_->cancel();
  }

 protected:
  MetricsManager() {}

  static constexpr const int kNumBuckets = 60;

  using Mutex = aws::mutex;
  using Lock = aws::lock_guard<Mutex>;

  struct MetricsCmp {
    bool operator()(std::shared_ptr<Metric>& a, std::shared_ptr<Metric>& b);
  };

  static inline std::string escape(const std::string& s) {
    return aws::utils::url_encode(s);
  }

  static inline std::string escape(double d) {
    return escape(std::to_string(d));
  }

  static boost::optional<std::string>
  generate_query_args(const Metric& m, int idx, boost::posix_time::ptime tp);

  void upload();

  void upload_one_batch(const std::string& query_str);

  void upload_with_path(const std::shared_ptr<std::string>& path);

  void handle_result(const std::shared_ptr<aws::http::HttpResult>& result);

  void retry_uploads();

  std::shared_ptr<aws::utils::Executor> executor_;
  std::string endpoint_;
  std::shared_ptr<aws::http::HttpClient> http_client_;
  std::shared_ptr<aws::auth::AwsCredentialsProvider> creds_;
  std::string region_;
  std::string cw_namespace_;
  constants::Level level_;
  constants::Granularity granularity_;
  detail::ExtraDimMap extra_dimensions_;
  std::chrono::milliseconds upload_frequency_;
  std::chrono::milliseconds retry_frequency_;

  MetricsIndex metrics_index_;

  std::shared_ptr<aws::utils::ScheduledCallback> scheduled_upload_;
  std::shared_ptr<aws::utils::ScheduledCallback> scheduled_retry_;

  Mutex mutex_;
  std::list<std::shared_ptr<std::string>> retryable_requests_;
};

class NullMetricsManager : public MetricsManager {
 public:
  NullMetricsManager()
      : dummy_(
            std::make_shared<Metric>(
                std::shared_ptr<Metric>(),
                std::pair<std::string, std::string>())) {
    extra_dimensions_[constants::Granularity::Global];
    extra_dimensions_[constants::Granularity::Stream];
    extra_dimensions_[constants::Granularity::Shard];
  }

  std::shared_ptr<Metric> get_metric(const MetricsFinder& finder) override {
    return dummy_;
  }

  std::vector<std::shared_ptr<Metric>> all_metrics() noexcept override {
    std::vector<std::shared_ptr<Metric>> v;
    v.push_back(dummy_);
    return v;
  }

  void stop() override {}

 private:
  std::shared_ptr<Metric> dummy_;
};

} //namespace metrics
} //namespace aws

#endif //AWS_METRICS_METRICS_MANAGER_H_
