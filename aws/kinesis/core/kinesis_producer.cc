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

#if BOOST_OS_WINDOWS
  #include <windows.h>
#else
  #include <sys/utsname.h>
#endif

#include <aws/utils/logging.h>
#include <aws/core/client/AWSClient.h>
#include <aws/core/client/DefaultRetryStrategy.h>
#include <aws/core/http/Scheme.h>
#include <aws/kinesis/core/kinesis_producer.h>

#include <system_error>
#include <aws/core/utils/threading/Executor.h>

namespace {

struct EndpointConfiguration {
  std::string kinesis_endpoint_;
  std::string cloudwatch_endpoint_;

  EndpointConfiguration(std::string kinesis_endpoint, std::string cloudwatch_endpoint) :
    kinesis_endpoint_(kinesis_endpoint), cloudwatch_endpoint_(cloudwatch_endpoint) {}
};

const constexpr char* kVersion = "0.14.0N";
const std::unordered_map< std::string, EndpointConfiguration > kRegionEndpointOverride = {
  { "cn-north-1", { "kinesis.cn-north-1.amazonaws.com.cn", "monitoring.cn-north-1.amazonaws.com.cn" } },
  { "cn-northwest-1", { "kinesis.cn-northwest-1.amazonaws.com.cn", "monitoring.cn-northwest-1.amazonaws.com.cn" } }
};
  const constexpr uint32_t kDefaultThreadPoolSize = 64;

void set_override_if_present(std::string& region, Aws::Client::ClientConfiguration& cfg, std::string service, std::function<std::string(EndpointConfiguration)> extractor) {
  auto region_override = kRegionEndpointOverride.find(region);
  if (region_override != kRegionEndpointOverride.end()) {
    std::string url = extractor(region_override->second);
    LOG(info) << "Found region override of " << service << " for " << region << ". Using endpoint of " << url;
    cfg.endpointOverride = url;
  } else {
    LOG(info) << "Using default " << service << " endpoint";
  }
}

std::string user_agent() {
  std::stringstream ss;
  ss << "KinesisProducerLibrary/" << kVersion << " | ";
#if BOOST_OS_WINDOWS
  OSVERSIONINFO v;
  v.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);
  GetVersionEx(&v);
  ss << "Windows | " << v.dwMajorVersion << "." << v.dwMinorVersion << "."
     << v.dwBuildNumber << "." << v.dwPlatformId;
#else
  ::utsname un;
  ::uname(&un);
  ss << un.sysname << " | " << un.release << " | " << un.version << " | "
     << un.machine;
#endif

  // Strip consecutive spaces
  auto ua = ss.str();
  auto new_end = std::unique(
      ua.begin(),
      ua.end(),
      [](auto a, auto b) {
        return a == ' ' && b == ' ';
      });
  ua.erase(new_end, ua.end());

  return ua;
}

template<typename T>
T cast_size_t(std::size_t value) {
  if (value > std::numeric_limits<T>::max()) {
    throw std::system_error(std::make_error_code(std::errc::result_out_of_range));
  }
  return static_cast<T>(value);
}

std::shared_ptr<Aws::Utils::Threading::Executor> sdk_client_executor;

Aws::Client::ClientConfiguration
make_sdk_client_cfg(const aws::kinesis::core::Configuration& kpl_cfg,
                    const std::string& region,
                    const std::string& ca_path, int retryCount) {
  Aws::Client::ClientConfiguration cfg;
  cfg.userAgent = user_agent();
  LOG(info) << "Using Region: " << region;
  cfg.region = region;
  cfg.maxConnections = cast_size_t<unsigned>(kpl_cfg.max_connections());
  cfg.requestTimeoutMs = cast_size_t<long>(kpl_cfg.request_timeout());
  cfg.connectTimeoutMs = cast_size_t<long>(kpl_cfg.connect_timeout());
  cfg.retryStrategy = std::make_shared<Aws::Client::DefaultRetryStrategy>(retryCount);
  cfg.proxyHost = kpl_cfg.proxy_host();
  cfg.proxyPort = cast_size_t<unsigned>(kpl_cfg.proxy_port());
  cfg.proxyUserName = kpl_cfg.proxy_user_name();
  cfg.proxyPassword = kpl_cfg.proxy_password();
  if (kpl_cfg.use_thread_pool()) {
    if (sdk_client_executor == nullptr) {
      uint32_t thread_pool_size = kpl_cfg.thread_pool_size();
      //
      // TODO: Add rlimit check to see if the configured thread pool size is greater than RLIMIT_NPROC, and report a warning.
      //
      if (thread_pool_size == 0) {
        thread_pool_size = kDefaultThreadPoolSize;
      }
      LOG(info) << "Using pooled threading model with " << thread_pool_size << " threads.";
      sdk_client_executor = std::make_shared<Aws::Utils::Threading::PooledThreadExecutor>(thread_pool_size);
    }
  } else {
    LOG(info) << "Using per request threading model.";
    sdk_client_executor = std::make_shared<Aws::Utils::Threading::DefaultExecutor>();
  }
  cfg.executor = sdk_client_executor;
  cfg.verifySSL = kpl_cfg.verify_certificate();
  cfg.caPath = ca_path;
  return cfg;
}

} //namespace

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
          cw_client_,
          config_->metrics_namespace(),
          level,
          granularity,
          std::move(extra_dims),
          std::chrono::milliseconds(config_->metrics_upload_delay()));
}

void KinesisProducer::create_kinesis_client(const std::string& ca_path) {
  auto cfg = make_sdk_client_cfg(*config_, region_, ca_path, 0);
  if (config_->kinesis_endpoint().size() > 0) {
    cfg.endpointOverride = config_->kinesis_endpoint() + ":" +
        std::to_string(config_->kinesis_port());
    LOG(info) << "Using Kinesis endpoint " + cfg.endpointOverride;
  } else {
      set_override_if_present(region_, cfg, "Kinesis", [](auto ep) { return ep.kinesis_endpoint_; });
  }

  kinesis_client_ = std::make_shared<Aws::Kinesis::KinesisClient>(
      kinesis_creds_provider_,
      cfg);
}

void KinesisProducer::create_cw_client(const std::string& ca_path) {
  auto cfg = make_sdk_client_cfg(*config_, region_, ca_path, 2);
  if (config_->cloudwatch_endpoint().size() > 0) {
    cfg.endpointOverride = config_->cloudwatch_endpoint() + ":" +
        std::to_string(config_->cloudwatch_port());
    LOG(info) << "Using CloudWatch endpoint " + cfg.endpointOverride;
  } else {
      set_override_if_present(region_, cfg, "CloudWatch", [](auto ep) -> std::string { return ep.cloudwatch_endpoint_; });
  }

  cw_client_ = std::make_shared<Aws::CloudWatch::CloudWatchClient>(
      cw_creds_provider_,
      cfg);
}

Pipeline* KinesisProducer::create_pipeline(const std::string& stream) {
  LOG(info) << "Created pipeline for stream \"" << stream << "\"";
  return new Pipeline(
      region_,
      stream,
      config_,
      executor_,
      kinesis_client_,
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
  const auto& akid = set_creds.credentials().akid();
  const auto& sk = set_creds.credentials().secret_key();
  auto token = set_creds.credentials().has_token()
      ? set_creds.credentials().token()
      : "";
  (set_creds.for_metrics() ? cw_creds_provider_ : kinesis_creds_provider_)
      ->set_credentials(akid, sk, token);
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
