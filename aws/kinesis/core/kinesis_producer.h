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

#ifndef AWS_KINESIS_CORE_KINESIS_PRODUCER_H_
#define AWS_KINESIS_CORE_KINESIS_PRODUCER_H_

#include <aws/auth/mutable_static_creds_provider.h>
#include <aws/kinesis/KinesisClient.h>
#include <aws/kinesis/core/pipeline.h>
#include <aws/metrics/metrics_manager.h>
#include <aws/monitoring/CloudWatchClient.h>

namespace aws {
namespace kinesis {
namespace core {

class KinesisProducer : boost::noncopyable {
 public:
  using Configuration = aws::kinesis::core::Configuration;

  KinesisProducer(
      std::shared_ptr<IpcManager> ipc_manager,
      std::string region,
      std::shared_ptr<Configuration>& config,
      std::shared_ptr<aws::auth::MutableStaticCredentialsProvider>
          kinesis_creds_provider,
      std::shared_ptr<aws::auth::MutableStaticCredentialsProvider>
          cw_creds_provider,
      std::shared_ptr<aws::utils::Executor> executor,
      std::string ca_path)
      : region_(std::move(region)),
        config_(std::move(config)),
        kinesis_creds_provider_(std::move(kinesis_creds_provider)),
        cw_creds_provider_(std::move(cw_creds_provider)),
        executor_(std::move(executor)),
        ipc_manager_(std::move(ipc_manager)),
        pipelines_([this](auto& stream) {
          return this->create_pipeline(stream);
        }),
        shutdown_(false) {
    create_kinesis_client(ca_path);
    create_cw_client(ca_path);
    create_metrics_manager();
    report_outstanding();
    message_drainer_ = aws::thread([this] { this->drain_messages(); });
  }

  ~KinesisProducer() {
    shutdown_ = true;
    message_drainer_.join();
  }

  void join() {
    executor_->join();
  }

 private:
  static const std::chrono::microseconds kMessageDrainMinBackoff;
  static const std::chrono::microseconds kMessageDrainMaxBackoff;
  static constexpr const size_t kMessageMaxBatchSize = 16;

  void create_metrics_manager();

  void create_kinesis_client(const std::string& ca_path);

  void create_cw_client(const std::string& ca_path);

  Pipeline* create_pipeline(const std::string& stream);

  void drain_messages();

  void on_ipc_message(std::string&& message) noexcept;

  void on_put_record(aws::kinesis::protobuf::Message& m);

  void on_flush(const aws::kinesis::protobuf::Flush& flush_msg);

  void on_metrics_request(const aws::kinesis::protobuf::Message& m);

  void on_set_credentials(
      const aws::kinesis::protobuf::SetCredentials& set_creds);

  void report_outstanding();

  std::string region_;

  std::shared_ptr<Configuration> config_;
  std::shared_ptr<aws::auth::MutableStaticCredentialsProvider>
      kinesis_creds_provider_;
  std::shared_ptr<aws::auth::MutableStaticCredentialsProvider>
      cw_creds_provider_;
  std::shared_ptr<Aws::Kinesis::KinesisClient> kinesis_client_;
  std::shared_ptr<Aws::CloudWatch::CloudWatchClient> cw_client_;
  std::shared_ptr<aws::utils::Executor> executor_;

  std::shared_ptr<IpcManager> ipc_manager_;
  std::shared_ptr<aws::metrics::MetricsManager> metrics_manager_;

  aws::utils::ConcurrentHashMap<std::string, Pipeline> pipelines_;
  bool shutdown_;
  aws::thread message_drainer_;

  std::shared_ptr<aws::utils::ScheduledCallback> report_outstanding_;
};

} //namespace core
} //namespace kinesis
} //namespace aws

#endif //AWS_KINESIS_CORE_KINESIS_PRODUCER_H_
