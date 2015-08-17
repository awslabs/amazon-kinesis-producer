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

#ifndef AWS_KINESIS_CORE_KINESIS_PRODUCER_H_
#define AWS_KINESIS_CORE_KINESIS_PRODUCER_H_

#include <aws/kinesis/core/pipeline.h>
#include <aws/metrics/metrics_manager.h>

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
      std::shared_ptr<aws::auth::AwsCredentialsProvider> creds_provider,
      std::shared_ptr<aws::auth::AwsCredentialsProvider> metrics_creds_provider,
      std::shared_ptr<aws::utils::Executor> executor,
      std::shared_ptr<aws::http::SocketFactory> socket_factory)
      : region_(std::move(region)),
        config_(std::move(config)),
        creds_chain_(
            aws::auth::AwsCredentialsProviderChain::create(
                {creds_provider})),
        // If metrics_creds_provider points to the same instance as
        // creds_provider, then the instances of AwsCredentialsProviderChain
        // should be the same too, this way when we get an update for
        // for creds_chain_, metrics_creds_chain_ is updated as well.
        metrics_creds_chain_(
            metrics_creds_provider == creds_provider
                ? creds_chain_
                : aws::auth::AwsCredentialsProviderChain::create(
                      {metrics_creds_provider})),
        executor_(std::move(executor)),
        socket_factory_(std::move(socket_factory)),
        ipc_manager_(std::move(ipc_manager)),
        pipelines_([this](auto& stream) {
          return this->create_pipeline(stream);
        }),
        shutdown_(false) {
    create_metrics_manager();
    create_http_client();
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

  void create_http_client();

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
  std::shared_ptr<aws::auth::AwsCredentialsProviderChain> creds_chain_;
  std::shared_ptr<aws::auth::AwsCredentialsProviderChain> metrics_creds_chain_;
  std::shared_ptr<aws::utils::Executor> executor_;
  std::shared_ptr<aws::http::SocketFactory> socket_factory_;

  std::shared_ptr<aws::http::HttpClient> http_client_;
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
