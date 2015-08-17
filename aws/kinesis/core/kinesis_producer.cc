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

#include <aws/kinesis/core/kinesis_producer.h>

namespace aws {
namespace kinesis {
namespace core {

const std::chrono::microseconds KinesisProducer::kMessageDrainMinBackoff(100);
const std::chrono::microseconds KinesisProducer::kMessageDrainMaxBackoff(10000);

void KinesisProducer::create_metrics_manager() {
  auto level = aws::metrics::constants::level(config_->metrics_level());
  auto granularity =
      aws::metrics::constants::granularity(config_->metrics_granularity());

  std::list<
      std::tuple<std::string,
                 std::string,
                 aws::metrics::constants::Granularity>> extra_dims;
  for (auto t : config_->additional_metrics_dims()) {
    extra_dims.push_back(
        std::make_tuple(
            std::get<0>(t),
            std::get<1>(t),
            aws::metrics::constants::granularity(std::get<2>(t))));
  }

  metrics_manager_ =
      std::make_shared<aws::metrics::MetricsManager>(
          executor_,
          socket_factory_,
          metrics_creds_chain_,
          region_,
          config_->metrics_namespace(),
          level,
          granularity,
          std::move(extra_dims),
          config_->custom_endpoint(),
          config_->port(),
          std::chrono::milliseconds(config_->metrics_upload_delay()));
}

void KinesisProducer::create_http_client() {
  auto endpoint = config_->custom_endpoint();
  auto port = config_->port();

  if (endpoint.empty()) {
    endpoint = "kinesis." + region_ + ".amazonaws.com";
    port = 443;
  }

  http_client_ =
      std::make_shared<aws::http::HttpClient>(
          executor_,
          socket_factory_,
          endpoint,
          port,
          true,
          config_->verify_certificate(),
          config_->min_connections(),
          config_->max_connections(),
          std::chrono::milliseconds(config_->connect_timeout()),
          std::chrono::milliseconds(config_->request_timeout()));
}

Pipeline* KinesisProducer::create_pipeline(const std::string& stream) {
  LOG(info) << "Created pipeline for stream \"" << stream << "\"";
  return new Pipeline(
      region_,
      stream,
      config_,
      executor_,
      http_client_,
      creds_chain_,
      metrics_manager_,
      [this](auto& ur) {
        ipc_manager_->put(ur->to_put_record_result().SerializeAsString());
      });
}

void KinesisProducer::drain_messages() {
  std::string s;
  std::vector<std::string> buf;
  std::chrono::microseconds backoff = kMessageDrainMinBackoff;

  while (!shutdown_) {
    // The checks must be in this order because try_take has a side effect
    while (buf.size() < kMessageMaxBatchSize && ipc_manager_->try_take(s)) {
      buf.push_back(std::move(s));
    }

    if (!buf.empty()) {
      std::vector<std::string> batch;
      std::swap(batch, buf);
      executor_->submit([batch = std::move(batch), this]() mutable {
        for (auto& s : batch) {
          this->on_ipc_message(std::move(s));
        }
      });
      backoff = kMessageDrainMinBackoff;
    } else {
      aws::utils::sleep_for(backoff);
      backoff = std::min(backoff * 2, kMessageDrainMaxBackoff);
    }
  }
}

void KinesisProducer::on_ipc_message(std::string&& message) noexcept {
  aws::kinesis::protobuf::Message m;
  try {
    m.ParseFromString(message);
  } catch (const std::exception& ex) {
    LOG(error) << "Unexpected error parsing ipc message: " << ex.what();
    return;
  }
  if (m.has_put_record()) {
    on_put_record(m);
  } else if (m.has_flush()) {
    on_flush(m.flush());
  } else if (m.has_metrics_request()) {
    on_metrics_request(m);
  } else if (m.has_set_credentials()) {
    on_set_credentials(m.set_credentials());
  } else {
    LOG(error) << "Received unknown message type";
  }
}

void KinesisProducer::on_put_record(aws::kinesis::protobuf::Message& m) {
  auto ur = std::make_shared<UserRecord>(m);
  ur->set_deadline_from_now(
      std::chrono::milliseconds(config_->record_max_buffered_time()));
  ur->set_expiration_from_now(
      std::chrono::milliseconds(config_->record_ttl()));
  pipelines_[ur->stream()].put(ur);
}

void KinesisProducer::on_flush(const aws::kinesis::protobuf::Flush& flush_msg) {
  if (flush_msg.has_stream_name()) {
    pipelines_[flush_msg.stream_name()].flush();
  } else {
    pipelines_.foreach([](auto&, auto pipeline) { pipeline->flush(); });
  }
}

void KinesisProducer::on_metrics_request(
    const aws::kinesis::protobuf::Message& m) {
  auto req = m.metrics_request();
  std::vector<std::shared_ptr<aws::metrics::Metric>> metrics;

  // filter by name, if necessary
  if (req.has_name()) {
    for (auto& metric : metrics_manager_->all_metrics()) {
      auto dims = metric->all_dimensions();

      assert(!dims.empty());
      assert(dims.at(0).first == "MetricName");

      if (dims.at(0).second == req.name()) {
        metrics.push_back(metric);
      }
    }
  } else {
    metrics = metrics_manager_->all_metrics();
  }

  // convert the data into protobuf
  aws::kinesis::protobuf::Message reply;
  reply.set_id(::rand());
  reply.set_source_id(m.id());
  auto res = reply.mutable_metrics_response();

  for (auto& metric : metrics) {
    auto dims = metric->all_dimensions();

    assert(!dims.empty());
    assert(dims.at(0).first == "MetricName");

    auto pm = res->add_metrics();
    pm->set_name(dims.at(0).second);

    for (size_t i = 1; i < dims.size(); i++) {
      auto d = pm->add_dimensions();
      d->set_key(dims[i].first);
      d->set_value(dims[i].second);
    }

    auto& accum = metric->accumulator();
    auto stats = pm->mutable_stats();

    if (req.has_seconds()) {
      auto s = req.seconds();
      stats->set_count(accum.count(s));
      stats->set_sum(accum.sum(s));
      stats->set_min(accum.min(s));
      stats->set_max(accum.max(s));
      stats->set_mean(accum.mean(s));
      pm->set_seconds(s);
    } else {
      stats->set_count(accum.count());
      stats->set_sum(accum.sum());
      stats->set_min(accum.min());
      stats->set_max(accum.max());
      stats->set_mean(accum.mean());
      pm->set_seconds(accum.elapsed<std::chrono::seconds>());
    }
  }

  ipc_manager_->put(reply.SerializeAsString());
}

void KinesisProducer::on_set_credentials(
    const aws::kinesis::protobuf::SetCredentials& set_creds) {
  // If the metrics_creds_chain_ is pointing to the same instance as the regular
  // creds_chain_, and we receive a new set of creds just for metrics, we need
  // to create a new instance of AwsCredentialsProviderChain for
  // metrics_creds_chain_ to point to. Otherwise we'll wrongly override the
  // regular credentials when setting the metrics credentials.
  if (set_creds.for_metrics() && metrics_creds_chain_ == creds_chain_) {
    metrics_creds_chain_ =
        std::make_shared<aws::auth::AwsCredentialsProviderChain>();
  }

  (set_creds.for_metrics()
      ? metrics_creds_chain_
      : creds_chain_)->reset({
          std::make_shared<aws::auth::BasicAwsCredentialsProvider>(
              set_creds.credentials().akid(),
              set_creds.credentials().secret_key(),
              set_creds.credentials().has_token()
                  ? boost::optional<std::string>(
                        set_creds.credentials().token())
                  : boost::none)});
}

void KinesisProducer::report_outstanding() {
  pipelines_.foreach([this](auto& stream, auto pipeline) {
    metrics_manager_
        ->finder()
        .set_name(aws::metrics::constants::Names::UserRecordsPending)
        .set_stream(stream)
        .find()
        ->put(pipeline->outstanding_user_records());
  });

  auto delay = std::chrono::milliseconds(200);
  if (!report_outstanding_) {
    report_outstanding_ =
        executor_->schedule(
            [this] { this->report_outstanding(); },
            delay);
  } else {
    report_outstanding_->reschedule(delay);
  }
}

} //namespace core
} //namespace kinesis
} //namespace aws
