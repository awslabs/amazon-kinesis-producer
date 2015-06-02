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

namespace detail {

class ProcessMessagesLoop : boost::noncopyable {
 public:
  using Callback = std::function<void (std::string&&) noexcept>;

  ProcessMessagesLoop(Callback callback,
                      std::shared_ptr<IpcManager> ipc_manager,
                      std::shared_ptr<aws::utils::Executor> executor)
      : run_([this] { this->run(); }),
        callback_(std::move(callback)),
        ipc_manager_(std::move(ipc_manager)),
        executor_(std::move(executor)) {
    run_();
  }

 private:
  void run();

  std::string tmp_;
  std::function<void ()> run_;
  Callback callback_;
  std::shared_ptr<IpcManager> ipc_manager_;
  std::shared_ptr<aws::utils::Executor> executor_;
  std::shared_ptr<aws::utils::ScheduledCallback> scheduled_callback_;
};

} //namespace detail

class KinesisProducer {
 public:
  using Configuration = aws::kinesis::core::Configuration;

  KinesisProducer(
      std::shared_ptr<IpcManager> ipc_manager,
      std::string region,
      std::shared_ptr<Configuration>& config,
      std::shared_ptr<aws::auth::AwsCredentialsProvider> creds_provider,
      std::shared_ptr<aws::utils::Executor> executor,
      std::shared_ptr<aws::http::SocketFactory> socket_factory)
      : region_(std::move(region)),
        config_(std::move(config)),
        creds_provider_(std::move(creds_provider)),
        executor_(std::move(executor)),
        socket_factory_(std::move(socket_factory)),
        ipc_manager_(std::move(ipc_manager)),
        pipelines_([this](auto& stream) {
          return this->create_pipeline(stream);
        }) {
    create_metrics_manager();
    create_http_client();
    start_message_loops();
    report_outstanding();
  }

  KinesisProducer(const KinesisProducer&) = delete;

  void join() {
    executor_->join();
  }

 private:
  void create_metrics_manager();

  void create_http_client();

  Pipeline* create_pipeline(const std::string& stream);

  void start_message_loops();

  void on_ipc_message(std::string&& message) noexcept;

  void on_put_record(aws::kinesis::protobuf::Message& m);

  void on_flush(const aws::kinesis::protobuf::Flush& flush_msg);

  void on_metrics_request(const aws::kinesis::protobuf::Message& m);

  void report_outstanding();

  std::string region_;

  std::shared_ptr<Configuration> config_;
  std::shared_ptr<aws::auth::AwsCredentialsProvider> creds_provider_;
  std::shared_ptr<aws::utils::Executor> executor_;
  std::shared_ptr<aws::http::SocketFactory> socket_factory_;

  std::shared_ptr<aws::http::HttpClient> http_client_;
  std::shared_ptr<IpcManager> ipc_manager_;
  std::shared_ptr<aws::metrics::MetricsManager> metrics_manager_;

  aws::utils::ConcurrentHashMap<std::string, Pipeline> pipelines_;
  std::vector<std::unique_ptr<detail::ProcessMessagesLoop>> message_loops_;

  std::shared_ptr<aws::utils::ScheduledCallback> report_outstanding_;
};

} //namespace core
} //namespace kinesis
} //namespace aws

#endif //AWS_KINESIS_CORE_KINESIS_PRODUCER_H_
