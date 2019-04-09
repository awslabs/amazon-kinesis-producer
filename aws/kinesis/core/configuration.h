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

#ifndef AWS_KINESIS_CORE_CONFIGURATION_H_
#define AWS_KINESIS_CORE_CONFIGURATION_H_

#include <regex>

#include <boost/noncopyable.hpp>

#include <aws/kinesis/protobuf/messages.pb.h>

namespace aws {
namespace kinesis {
namespace core {

// This class is generated with config_generator.py, do not edit by hand.
class Configuration : private boost::noncopyable {
 public:

  // Enable aggregation. With aggregation, multiple user records are packed
  // into a single KinesisRecord. If disabled, each user record is sent in its
  // own KinesisRecord.
  //
  // If your records are small, enabling aggregation will allow you to put
  // many more records than you would otherwise be able to for a shard before
  // getting throttled.
  //
  // Default: true
  bool aggregation_enabled() const noexcept {
    return aggregation_enabled_;
  }

  // Maximum number of items to pack into an aggregated record.
  //
  // There should be normally no need to adjust this. If you want to limit the
  // time records spend buffering, look into record_max_buffered_time instead.
  //
  // Default: 4294967295
  // Minimum: 1
  // Maximum (inclusive): 9223372036854775807
  size_t aggregation_max_count() const noexcept {
    return aggregation_max_count_;
  }

  // Maximum number of bytes to pack into an aggregated Kinesis record.
  //
  // There should be normally no need to adjust this. If you want to limit the
  // time records spend buffering, look into record_max_buffered_time instead.
  //
  // If a record has more data by itself than this limit, it will bypass the
  // aggregator. Note the backend enforces a limit of 50KB on record size. If
  // you set this beyond 50KB, oversize records will be rejected at the
  // backend.
  //
  // Default: 51200
  // Minimum: 64
  // Maximum (inclusive): 1048576
  size_t aggregation_max_size() const noexcept {
    return aggregation_max_size_;
  }

  // Use a custom CloudWatch endpoint.
  //
  // Note this does not accept protocols or paths, only host names or ip
  // addresses. There is no way to disable TLS. The KPL always connects with
  // TLS.
  //
  // Expected pattern: ^([A-Za-z0-9-\\.]+)?$
  const std::string& cloudwatch_endpoint() const noexcept {
    return cloudwatch_endpoint_;
  }

  // Server port to connect to for CloudWatch.
  //
  // Default: 443
  // Minimum: 1
  // Maximum (inclusive): 65535
  size_t cloudwatch_port() const noexcept {
    return cloudwatch_port_;
  }

  // Maximum number of items to pack into an PutRecords request.
  //
  // There should be normally no need to adjust this. If you want to limit the
  // time records spend buffering, look into record_max_buffered_time instead.
  //
  // Default: 500
  // Minimum: 1
  // Maximum (inclusive): 500
  size_t collection_max_count() const noexcept {
    return collection_max_count_;
  }

  // Maximum amount of data to send with a PutRecords request.
  //
  // There should be normally no need to adjust this. If you want to limit the
  // time records spend buffering, look into record_max_buffered_time instead.
  //
  // Records larger than the limit will still be sent, but will not be grouped
  // with others.
  //
  // Default: 5242880
  // Minimum: 52224
  // Maximum (inclusive): 9223372036854775807
  size_t collection_max_size() const noexcept {
    return collection_max_size_;
  }

  // Timeout (milliseconds) for establishing TLS connections.
  //
  // Default: 6000
  // Minimum: 100
  // Maximum (inclusive): 300000
  uint64_t connect_timeout() const noexcept {
    return connect_timeout_;
  }

  // This has no effect on Windows.
  //
  // If set to true, the KPL native process will attempt to raise its own core
  // file size soft limit to 128MB, or the hard limit, whichever is lower. If
  // the soft limit is already at or above the target amount, it is not
  // changed.
  //
  // Note that even if the limit is successfully raised (or already
  // sufficient), it does not guarantee that core files will be written on a
  // crash, since that is dependent on operation system settings that's beyond
  // the control of individual processes.
  //
  // Default: false
  bool enable_core_dumps() const noexcept {
    return enable_core_dumps_;
  }

  // If true, throttled puts are not retried. The records that got throttled
  // will be failed immediately upon receiving the throttling error. This is
  // useful if you want to react immediately to any throttling without waiting
  // for the KPL to retry. For example, you can use a different hash key to
  // send the throttled record to a backup shard.
  //
  // If false, the KPL will automatically retry throttled puts. The KPL
  // performs backoff for shards that it has received throttling errors from,
  // and will avoid flooding them with retries. Note that records may fail
  // from expiration (see record_ttl) if they get delayed for too long because
  // of throttling.
  //
  // Default: false
  bool fail_if_throttled() const noexcept {
    return fail_if_throttled_;
  }

  // Use a custom Kinesis endpoint.
  //
  // Note this does not accept protocols or paths, only host names or ip
  // addresses. There is no way to disable TLS. The KPL always connects with
  // TLS.
  //
  // Expected pattern: ^([A-Za-z0-9-\\.]+)?$
  const std::string& kinesis_endpoint() const noexcept {
    return kinesis_endpoint_;
  }

  // Server port to connect to for Kinesis.
  //
  // Default: 443
  // Minimum: 1
  // Maximum (inclusive): 65535
  size_t kinesis_port() const noexcept {
    return kinesis_port_;
  }

  // Minimum level of logs. Messages below the specified level will not be
  // logged. Logs for the native KPL daemon show up on stderr.
  //
  // Default: info
  // Expected pattern: info|warning|error
  const std::string& log_level() const noexcept {
    return log_level_;
  }

  // Maximum number of connections to open to the backend. HTTP requests are
  // sent in parallel over multiple connections.
  //
  // Setting this too high may impact latency and consume additional resources
  // without increasing throughput.
  //
  // Default: 24
  // Minimum: 1
  // Maximum (inclusive): 256
  size_t max_connections() const noexcept {
    return max_connections_;
  }

  // Controls the granularity of metrics that are uploaded to CloudWatch.
  // Greater granularity produces more metrics.
  //
  // When "shard" is selected, metrics are emitted with the stream name and
  // shard id as dimensions. On top of this, the same metric is also emitted
  // with only the stream name dimension, and lastly, without the stream name.
  // This means for a particular metric, 2 streams with 2 shards (each) will
  // produce 7 CloudWatch metrics, one for each shard, one for each stream,
  // and one overall, all describing the same statistics, but at different
  // levels of granularity.
  //
  // When "stream" is selected, per shard metrics are not uploaded; when
  // "global" is selected, only the total aggregate for all streams and all
  // shards are uploaded.
  //
  // Consider reducing the granularity if you're not interested in shard-level
  // metrics, or if you have a large number of shards.
  //
  // If you only have 1 stream, select "global"; the global data will be
  // equivalent to that for the stream.
  //
  // Refer to the metrics documentation for details about each metric.
  //
  // Default: shard
  // Expected pattern: global|stream|shard
  const std::string& metrics_granularity() const noexcept {
    return metrics_granularity_;
  }

  // Controls the number of metrics that are uploaded to CloudWatch.
  //
  // "none" disables all metrics.
  //
  // "summary" enables the following metrics: UserRecordsPut,
  // KinesisRecordsPut, ErrorsByCode, AllErrors, BufferingTime.
  //
  // "detailed" enables all remaining metrics.
  //
  // Refer to the metrics documentation for details about each metric.
  //
  // Default: detailed
  // Expected pattern: none|summary|detailed
  const std::string& metrics_level() const noexcept {
    return metrics_level_;
  }

  // The namespace to upload metrics under.
  //
  // If you have multiple applications running the KPL under the same AWS
  // account, you should use a different namespace for each application.
  //
  // If you are also using the KCL, you may wish to use the application name
  // you have configured for the KCL as the the namespace here. This way both
  // your KPL and KCL metrics show up under the same namespace.
  //
  // Default: KinesisProducerLibrary
  // Expected pattern: (?!AWS/).{1,255}
  const std::string& metrics_namespace() const noexcept {
    return metrics_namespace_;
  }

  // Delay (in milliseconds) between each metrics upload.
  //
  // For testing only. There is no benefit in setting this lower or higher in
  // production.
  //
  // Default: 60000
  // Minimum: 1
  // Maximum (inclusive): 60000
  size_t metrics_upload_delay() const noexcept {
    return metrics_upload_delay_;
  }

  // Minimum number of connections to keep open to the backend.
  //
  // There should be no need to increase this in general.
  //
  // Default: 1
  // Minimum: 1
  // Maximum (inclusive): 16
  size_t min_connections() const noexcept {
    return min_connections_;
  }

  // Limits the maximum allowed put rate for a shard, as a percentage of the
  // backend limits.
  //
  // The rate limit prevents the producer from sending data too fast to a
  // shard. Such a limit is useful for reducing bandwidth and CPU cycle
  // wastage from sending requests that we know are going to fail from
  // throttling.
  //
  // Kinesis enforces limits on both the number of records and number of bytes
  // per second. This setting applies to both.
  //
  // The default value of 150% is chosen to allow a single producer instance
  // to completely saturate the allowance for a shard. This is an aggressive
  // setting. If you prefer to reduce throttling errors rather than completely
  // saturate the shard, consider reducing this setting.
  //
  // Default: 150
  // Minimum: 1
  // Maximum (inclusive): 9223372036854775807
  size_t rate_limit() const noexcept {
    return rate_limit_;
  }

  // Maximum amount of itme (milliseconds) a record may spend being buffered
  // before it gets sent. Records may be sent sooner than this depending on
  // the other buffering limits.
  //
  // This setting provides coarse ordering among records - any two records
  // will be reordered by no more than twice this amount (assuming no failures
  // and retries and equal network latency).
  //
  // The library makes a best effort to enforce this time, but cannot
  // guarantee that it will be precisely met. In general, if the CPU is not
  // overloaded, the library will meet this deadline to within 10ms.
  //
  // Failures and retries can additionally increase the amount of time records
  // spend in the KPL. If your application cannot tolerate late records, use
  // the record_ttl setting to drop records that do not get transmitted in
  // time.
  //
  // Setting this too low can negatively impact throughput.
  //
  // Default: 100
  // Maximum (inclusive): 9223372036854775807
  uint64_t record_max_buffered_time() const noexcept {
    return record_max_buffered_time_;
  }

  // Set a time-to-live on records (milliseconds). Records that do not get
  // successfully put within the limit are failed.
  //
  // This setting is useful if your application cannot or does not wish to
  // tolerate late records. Records will still incur network latency after
  // they leave the KPL, so take that into consideration when choosing a value
  // for this setting.
  //
  // If you do not wish to lose records and prefer to retry indefinitely, set
  // record_ttl to a large value like INT_MAX. This has the potential to cause
  // head-of-line blocking if network issues or throttling occur. You can
  // respond to such situations by using the metrics reporting functions of
  // the KPL. You may also set fail_if_throttled to true to prevent automatic
  // retries in case of throttling.
  //
  // Default: 30000
  // Minimum: 100
  // Maximum (inclusive): 9223372036854775807
  uint64_t record_ttl() const noexcept {
    return record_ttl_;
  }

  // Which region to send records to.
  //
  // If you do not specify the region and are running in EC2, the library will
  // use the region the instance is in.
  //
  // The region is also used to sign requests.
  //
  // Expected pattern: ^([a-z]+-([a-z]+-)?[a-z]+-[0-9])?$
  const std::string& region() const noexcept {
    return region_;
  }

  // The maximum total time (milliseconds) elapsed between when we begin a
  // HTTP request and receiving all of the response. If it goes over, the
  // request will be timed-out.
  //
  // Note that a timed-out request may actually succeed at the backend.
  // Retrying then leads to duplicates. Setting the timeout too low will
  // therefore increase the probability of duplicates.
  //
  // Default: 6000
  // Minimum: 100
  // Maximum (inclusive): 600000
  uint64_t request_timeout() const noexcept {
    return request_timeout_;
  }

  // Verify SSL certificates. Always enable in production for security.
  //
  // Default: true
  bool verify_certificate() const noexcept {
    return verify_certificate_;
  }

  /// Indicates whether the SDK clients should use a thread pool or not
  /// \return true if the client should use a thread pool, false otherwise
  bool use_thread_pool() const noexcept {
    return use_thread_pool_;
  }

  /// The maximum number of threads that a thread pool should be limited to.
  /// Threads are created eagerly.  This is only relevant if \see use_thread_pool() is true
  /// \return the mamximum number of threads that the thread pool should consume
  uint32_t thread_pool_size() const noexcept {
    return thread_pool_size_;
  }

  // Enable aggregation. With aggregation, multiple user records are packed
  // into a single KinesisRecord. If disabled, each user record is sent in its
  // own KinesisRecord.
  //
  // If your records are small, enabling aggregation will allow you to put
  // many more records than you would otherwise be able to for a shard before
  // getting throttled.
  //
  // Default: true
  Configuration& aggregation_enabled(bool val) {
    aggregation_enabled_ = val;
    return *this;
  }

  // Maximum number of items to pack into an aggregated record.
  //
  // There should be normally no need to adjust this. If you want to limit the
  // time records spend buffering, look into record_max_buffered_time instead.
  //
  // Default: 4294967295
  // Minimum: 1
  // Maximum (inclusive): 9223372036854775807
  Configuration& aggregation_max_count(size_t val) {
    if (val < 1ull || val > 9223372036854775807ull) {
      std::string err;
      err += "aggregation_max_count must be between 1 and 9223372036854775807, got ";
      err += std::to_string(val);
      throw std::runtime_error(err);
    }
    aggregation_max_count_ = val;
    return *this;
  }

  // Maximum number of bytes to pack into an aggregated Kinesis record.
  //
  // There should be normally no need to adjust this. If you want to limit the
  // time records spend buffering, look into record_max_buffered_time instead.
  //
  // If a record has more data by itself than this limit, it will bypass the
  // aggregator. Note the backend enforces a limit of 50KB on record size. If
  // you set this beyond 50KB, oversize records will be rejected at the
  // backend.
  //
  // Default: 51200
  // Minimum: 64
  // Maximum (inclusive): 1048576
  Configuration& aggregation_max_size(size_t val) {
    if (val < 64ull || val > 1048576ull) {
      std::string err;
      err += "aggregation_max_size must be between 64 and 1048576, got ";
      err += std::to_string(val);
      throw std::runtime_error(err);
    }
    aggregation_max_size_ = val;
    return *this;
  }

  // Use a custom CloudWatch endpoint.
  //
  // Note this does not accept protocols or paths, only host names or ip
  // addresses. There is no way to disable TLS. The KPL always connects with
  // TLS.
  //
  // Expected pattern: ^([A-Za-z0-9-\\.]+)?$
  Configuration& cloudwatch_endpoint(std::string val) {
    static std::regex pattern(
        "^([A-Za-z0-9-\\.]+)?$",
        std::regex::ECMAScript | std::regex::optimize);
    if (!std::regex_match(val, pattern)) {
      std::string err;
      err += "cloudwatch_endpoint must match the pattern ^([A-Za-z0-9-\\.]+)?$, got ";
      err += val;
      throw std::runtime_error(err);
    }
    cloudwatch_endpoint_ = val;
    return *this;
  }

  // Server port to connect to for CloudWatch.
  //
  // Default: 443
  // Minimum: 1
  // Maximum (inclusive): 65535
  Configuration& cloudwatch_port(size_t val) {
    if (val < 1ull || val > 65535ull) {
      std::string err;
      err += "cloudwatch_port must be between 1 and 65535, got ";
      err += std::to_string(val);
      throw std::runtime_error(err);
    }
    cloudwatch_port_ = val;
    return *this;
  }

  // Maximum number of items to pack into an PutRecords request.
  //
  // There should be normally no need to adjust this. If you want to limit the
  // time records spend buffering, look into record_max_buffered_time instead.
  //
  // Default: 500
  // Minimum: 1
  // Maximum (inclusive): 500
  Configuration& collection_max_count(size_t val) {
    if (val < 1ull || val > 500ull) {
      std::string err;
      err += "collection_max_count must be between 1 and 500, got ";
      err += std::to_string(val);
      throw std::runtime_error(err);
    }
    collection_max_count_ = val;
    return *this;
  }

  // Maximum amount of data to send with a PutRecords request.
  //
  // There should be normally no need to adjust this. If you want to limit the
  // time records spend buffering, look into record_max_buffered_time instead.
  //
  // Records larger than the limit will still be sent, but will not be grouped
  // with others.
  //
  // Default: 5242880
  // Minimum: 52224
  // Maximum (inclusive): 9223372036854775807
  Configuration& collection_max_size(size_t val) {
    if (val < 52224ull || val > 9223372036854775807ull) {
      std::string err;
      err += "collection_max_size must be between 52224 and 9223372036854775807, got ";
      err += std::to_string(val);
      throw std::runtime_error(err);
    }
    collection_max_size_ = val;
    return *this;
  }

  // Timeout (milliseconds) for establishing TLS connections.
  //
  // Default: 6000
  // Minimum: 100
  // Maximum (inclusive): 300000
  Configuration& connect_timeout(uint64_t val) {
    if (val < 100ull || val > 300000ull) {
      std::string err;
      err += "connect_timeout must be between 100 and 300000, got ";
      err += std::to_string(val);
      throw std::runtime_error(err);
    }
    connect_timeout_ = val;
    return *this;
  }

  // This has no effect on Windows.
  //
  // If set to true, the KPL native process will attempt to raise its own core
  // file size soft limit to 128MB, or the hard limit, whichever is lower. If
  // the soft limit is already at or above the target amount, it is not
  // changed.
  //
  // Note that even if the limit is successfully raised (or already
  // sufficient), it does not guarantee that core files will be written on a
  // crash, since that is dependent on operation system settings that's beyond
  // the control of individual processes.
  //
  // Default: false
  Configuration& enable_core_dumps(bool val) {
    enable_core_dumps_ = val;
    return *this;
  }

  // If true, throttled puts are not retried. The records that got throttled
  // will be failed immediately upon receiving the throttling error. This is
  // useful if you want to react immediately to any throttling without waiting
  // for the KPL to retry. For example, you can use a different hash key to
  // send the throttled record to a backup shard.
  //
  // If false, the KPL will automatically retry throttled puts. The KPL
  // performs backoff for shards that it has received throttling errors from,
  // and will avoid flooding them with retries. Note that records may fail
  // from expiration (see record_ttl) if they get delayed for too long because
  // of throttling.
  //
  // Default: false
  Configuration& fail_if_throttled(bool val) {
    fail_if_throttled_ = val;
    return *this;
  }

  // Use a custom Kinesis endpoint.
  //
  // Note this does not accept protocols or paths, only host names or ip
  // addresses. There is no way to disable TLS. The KPL always connects with
  // TLS.
  //
  // Expected pattern: ^([A-Za-z0-9-\\.]+)?$
  Configuration& kinesis_endpoint(std::string val) {
    static std::regex pattern(
        "^([A-Za-z0-9-\\.]+)?$",
        std::regex::ECMAScript | std::regex::optimize);
    if (!std::regex_match(val, pattern)) {
      std::string err;
      err += "kinesis_endpoint must match the pattern ^([A-Za-z0-9-\\.]+)?$, got ";
      err += val;
      throw std::runtime_error(err);
    }
    kinesis_endpoint_ = val;
    return *this;
  }

  // Server port to connect to for Kinesis.
  //
  // Default: 443
  // Minimum: 1
  // Maximum (inclusive): 65535
  Configuration& kinesis_port(size_t val) {
    if (val < 1ull || val > 65535ull) {
      std::string err;
      err += "kinesis_port must be between 1 and 65535, got ";
      err += std::to_string(val);
      throw std::runtime_error(err);
    }
    kinesis_port_ = val;
    return *this;
  }

  // Minimum level of logs. Messages below the specified level will not be
  // logged. Logs for the native KPL daemon show up on stderr.
  //
  // Default: info
  // Expected pattern: info|warning|error
  Configuration& log_level(std::string val) {
    static std::regex pattern(
        "info|warning|error",
        std::regex::ECMAScript | std::regex::optimize);
    if (!std::regex_match(val, pattern)) {
      std::string err;
      err += "log_level must match the pattern info|warning|error, got ";
      err += val;
      throw std::runtime_error(err);
    }
    log_level_ = val;
    return *this;
  }

  // Maximum number of connections to open to the backend. HTTP requests are
  // sent in parallel over multiple connections.
  //
  // Setting this too high may impact latency and consume additional resources
  // without increasing throughput.
  //
  // Default: 24
  // Minimum: 1
  // Maximum (inclusive): 256
  Configuration& max_connections(size_t val) {
    if (val < 1ull || val > 256ull) {
      std::string err;
      err += "max_connections must be between 1 and 256, got ";
      err += std::to_string(val);
      throw std::runtime_error(err);
    }
    max_connections_ = val;
    return *this;
  }

  // Controls the granularity of metrics that are uploaded to CloudWatch.
  // Greater granularity produces more metrics.
  //
  // When "shard" is selected, metrics are emitted with the stream name and
  // shard id as dimensions. On top of this, the same metric is also emitted
  // with only the stream name dimension, and lastly, without the stream name.
  // This means for a particular metric, 2 streams with 2 shards (each) will
  // produce 7 CloudWatch metrics, one for each shard, one for each stream,
  // and one overall, all describing the same statistics, but at different
  // levels of granularity.
  //
  // When "stream" is selected, per shard metrics are not uploaded; when
  // "global" is selected, only the total aggregate for all streams and all
  // shards are uploaded.
  //
  // Consider reducing the granularity if you're not interested in shard-level
  // metrics, or if you have a large number of shards.
  //
  // If you only have 1 stream, select "global"; the global data will be
  // equivalent to that for the stream.
  //
  // Refer to the metrics documentation for details about each metric.
  //
  // Default: shard
  // Expected pattern: global|stream|shard
  Configuration& metrics_granularity(std::string val) {
    static std::regex pattern(
        "global|stream|shard",
        std::regex::ECMAScript | std::regex::optimize);
    if (!std::regex_match(val, pattern)) {
      std::string err;
      err += "metrics_granularity must match the pattern global|stream|shard, got ";
      err += val;
      throw std::runtime_error(err);
    }
    metrics_granularity_ = val;
    return *this;
  }

  // Controls the number of metrics that are uploaded to CloudWatch.
  //
  // "none" disables all metrics.
  //
  // "summary" enables the following metrics: UserRecordsPut,
  // KinesisRecordsPut, ErrorsByCode, AllErrors, BufferingTime.
  //
  // "detailed" enables all remaining metrics.
  //
  // Refer to the metrics documentation for details about each metric.
  //
  // Default: detailed
  // Expected pattern: none|summary|detailed
  Configuration& metrics_level(std::string val) {
    static std::regex pattern(
        "none|summary|detailed",
        std::regex::ECMAScript | std::regex::optimize);
    if (!std::regex_match(val, pattern)) {
      std::string err;
      err += "metrics_level must match the pattern none|summary|detailed, got ";
      err += val;
      throw std::runtime_error(err);
    }
    metrics_level_ = val;
    return *this;
  }

  // The namespace to upload metrics under.
  //
  // If you have multiple applications running the KPL under the same AWS
  // account, you should use a different namespace for each application.
  //
  // If you are also using the KCL, you may wish to use the application name
  // you have configured for the KCL as the the namespace here. This way both
  // your KPL and KCL metrics show up under the same namespace.
  //
  // Default: KinesisProducerLibrary
  // Expected pattern: (?!AWS/).{1,255}
  Configuration& metrics_namespace(std::string val) {
    static std::regex pattern(
        "(?!AWS/).{1,255}",
        std::regex::ECMAScript | std::regex::optimize);
    if (!std::regex_match(val, pattern)) {
      std::string err;
      err += "metrics_namespace must match the pattern (?!AWS/).{1,255}, got ";
      err += val;
      throw std::runtime_error(err);
    }
    metrics_namespace_ = val;
    return *this;
  }

  // Delay (in milliseconds) between each metrics upload.
  //
  // For testing only. There is no benefit in setting this lower or higher in
  // production.
  //
  // Default: 60000
  // Minimum: 1
  // Maximum (inclusive): 60000
  Configuration& metrics_upload_delay(size_t val) {
    if (val < 1ull || val > 60000ull) {
      std::string err;
      err += "metrics_upload_delay must be between 1 and 60000, got ";
      err += std::to_string(val);
      throw std::runtime_error(err);
    }
    metrics_upload_delay_ = val;
    return *this;
  }

  // Minimum number of connections to keep open to the backend.
  //
  // There should be no need to increase this in general.
  //
  // Default: 1
  // Minimum: 1
  // Maximum (inclusive): 16
  Configuration& min_connections(size_t val) {
    if (val < 1ull || val > 16ull) {
      std::string err;
      err += "min_connections must be between 1 and 16, got ";
      err += std::to_string(val);
      throw std::runtime_error(err);
    }
    min_connections_ = val;
    return *this;
  }

  // Limits the maximum allowed put rate for a shard, as a percentage of the
  // backend limits.
  //
  // The rate limit prevents the producer from sending data too fast to a
  // shard. Such a limit is useful for reducing bandwidth and CPU cycle
  // wastage from sending requests that we know are going to fail from
  // throttling.
  //
  // Kinesis enforces limits on both the number of records and number of bytes
  // per second. This setting applies to both.
  //
  // The default value of 150% is chosen to allow a single producer instance
  // to completely saturate the allowance for a shard. This is an aggressive
  // setting. If you prefer to reduce throttling errors rather than completely
  // saturate the shard, consider reducing this setting.
  //
  // Default: 150
  // Minimum: 1
  // Maximum (inclusive): 9223372036854775807
  Configuration& rate_limit(size_t val) {
    if (val < 1ull || val > 9223372036854775807ull) {
      std::string err;
      err += "rate_limit must be between 1 and 9223372036854775807, got ";
      err += std::to_string(val);
      throw std::runtime_error(err);
    }
    rate_limit_ = val;
    return *this;
  }

  // Maximum amount of itme (milliseconds) a record may spend being buffered
  // before it gets sent. Records may be sent sooner than this depending on
  // the other buffering limits.
  //
  // This setting provides coarse ordering among records - any two records
  // will be reordered by no more than twice this amount (assuming no failures
  // and retries and equal network latency).
  //
  // The library makes a best effort to enforce this time, but cannot
  // guarantee that it will be precisely met. In general, if the CPU is not
  // overloaded, the library will meet this deadline to within 10ms.
  //
  // Failures and retries can additionally increase the amount of time records
  // spend in the KPL. If your application cannot tolerate late records, use
  // the record_ttl setting to drop records that do not get transmitted in
  // time.
  //
  // Setting this too low can negatively impact throughput.
  //
  // Default: 100
  // Maximum (inclusive): 9223372036854775807
  Configuration& record_max_buffered_time(uint64_t val) {
    if (val > 9223372036854775807ull) {
      std::string err;
      err += "record_max_buffered_time must be between 0 and 9223372036854775807, got ";
      err += std::to_string(val);
      throw std::runtime_error(err);
    }
    record_max_buffered_time_ = val;
    return *this;
  }

  // Set a time-to-live on records (milliseconds). Records that do not get
  // successfully put within the limit are failed.
  //
  // This setting is useful if your application cannot or does not wish to
  // tolerate late records. Records will still incur network latency after
  // they leave the KPL, so take that into consideration when choosing a value
  // for this setting.
  //
  // If you do not wish to lose records and prefer to retry indefinitely, set
  // record_ttl to a large value like INT_MAX. This has the potential to cause
  // head-of-line blocking if network issues or throttling occur. You can
  // respond to such situations by using the metrics reporting functions of
  // the KPL. You may also set fail_if_throttled to true to prevent automatic
  // retries in case of throttling.
  //
  // Default: 30000
  // Minimum: 100
  // Maximum (inclusive): 9223372036854775807
  Configuration& record_ttl(uint64_t val) {
    if (val < 100ull || val > 9223372036854775807ull) {
      std::string err;
      err += "record_ttl must be between 100 and 9223372036854775807, got ";
      err += std::to_string(val);
      throw std::runtime_error(err);
    }
    record_ttl_ = val;
    return *this;
  }

  // Which region to send records to.
  //
  // If you do not specify the region and are running in EC2, the library will
  // use the region the instance is in.
  //
  // The region is also used to sign requests.
  //
  // Expected pattern: ^([a-z]+-([a-z]+-)?[a-z]+-[0-9])?$
  Configuration& region(std::string val) {
    static std::regex pattern(
        "^([a-z]+-([a-z]+-)?[a-z]+-[0-9])?$",
        std::regex::ECMAScript | std::regex::optimize);
    if (!std::regex_match(val, pattern)) {
      std::string err;
      err += "region must match the pattern ^([a-z]+-([a-z]+-)?[a-z]+-[0-9])?$, got ";
      err += val;
      throw std::runtime_error(err);
    }
    region_ = val;
    return *this;
  }

  // The maximum total time (milliseconds) elapsed between when we begin a
  // HTTP request and receiving all of the response. If it goes over, the
  // request will be timed-out.
  //
  // Note that a timed-out request may actually succeed at the backend.
  // Retrying then leads to duplicates. Setting the timeout too low will
  // therefore increase the probability of duplicates.
  //
  // Default: 6000
  // Minimum: 100
  // Maximum (inclusive): 600000
  Configuration& request_timeout(uint64_t val) {
    if (val < 100ull || val > 600000ull) {
      std::string err;
      err += "request_timeout must be between 100 and 600000, got ";
      err += std::to_string(val);
      throw std::runtime_error(err);
    }
    request_timeout_ = val;
    return *this;
  }

  // Verify SSL certificates. Always enable in production for security.
  //
  // Default: true
  Configuration& verify_certificate(bool val) {
    verify_certificate_ = val;
    return *this;
  }

  /// Enables or disable the use of a thread pool for the SDK Client.
  /// Default: false
  /// \param val whether or not to use a thread pool
  /// \return This configuration
  Configuration& use_thread_pool(bool val) {
    use_thread_pool_ = val;
    return *this;
  }

  /// The maximum number of threads the thread pool will be allowed to use.
  /// This is only useful if \see use_thread_pool is set to true
  /// The threads for the thread pool are allocated eagerly.
  /// \param val the maximum number of threads that the thread pool will use
  /// \return This configuration
  Configuration& thread_pool_size(uint32_t val) {
    if (val > 0) {
      thread_pool_size_ = val;
    }
    return *this;
  }


  const std::vector<std::tuple<std::string, std::string, std::string>>&
  additional_metrics_dims() {
    return additional_metrics_dims_;
  }

  void add_additional_metrics_dims(std::string key,
                                   std::string value,
                                   std::string granularity) {
    additional_metrics_dims_.emplace_back(std::move(key),
                                          std::move(value),
                                          std::move(granularity));
  }

  void transfer_from_protobuf_msg(const aws::kinesis::protobuf::Message& m) {
    if (!m.has_configuration()) {
      throw std::runtime_error("Not a configuration message");
    }
    auto c = m.configuration();
    aggregation_enabled(c.aggregation_enabled());
    aggregation_max_count(c.aggregation_max_count());
    aggregation_max_size(c.aggregation_max_size());
    cloudwatch_endpoint(c.cloudwatch_endpoint());
    cloudwatch_port(c.cloudwatch_port());
    collection_max_count(c.collection_max_count());
    collection_max_size(c.collection_max_size());
    connect_timeout(c.connect_timeout());
    enable_core_dumps(c.enable_core_dumps());
    fail_if_throttled(c.fail_if_throttled());
    kinesis_endpoint(c.kinesis_endpoint());
    kinesis_port(c.kinesis_port());
    log_level(c.log_level());
    max_connections(c.max_connections());
    metrics_granularity(c.metrics_granularity());
    metrics_level(c.metrics_level());
    metrics_namespace(c.metrics_namespace());
    metrics_upload_delay(c.metrics_upload_delay());
    min_connections(c.min_connections());
    rate_limit(c.rate_limit());
    record_max_buffered_time(c.record_max_buffered_time());
    record_ttl(c.record_ttl());
    region(c.region());
    request_timeout(c.request_timeout());
    verify_certificate(c.verify_certificate());
    if (c.thread_config() == ::aws::kinesis::protobuf::Configuration_ThreadConfig::Configuration_ThreadConfig_POOLED) {
      use_thread_pool(true);
      thread_pool_size(c.thread_pool_size());
    } else if (c.thread_config() == ::aws::kinesis::protobuf::Configuration_ThreadConfig_PER_REQUEST) {
      use_thread_pool(false);
    }

    for (auto i = 0; i < c.additional_metric_dims_size(); i++) {
      auto ad = c.additional_metric_dims(i);
      additional_metrics_dims_.push_back(
          std::make_tuple(ad.key(), ad.value(), ad.granularity()));
    }

  }

 private:
  bool aggregation_enabled_ = true;
  size_t aggregation_max_count_ = 4294967295;
  size_t aggregation_max_size_ = 51200;
  std::string cloudwatch_endpoint_ = "";
  size_t cloudwatch_port_ = 443;
  size_t collection_max_count_ = 500;
  size_t collection_max_size_ = 5242880;
  uint64_t connect_timeout_ = 6000;
  bool enable_core_dumps_ = false;
  bool fail_if_throttled_ = false;
  std::string kinesis_endpoint_ = "";
  size_t kinesis_port_ = 443;
  std::string log_level_ = "info";
  size_t max_connections_ = 24;
  std::string metrics_granularity_ = "shard";
  std::string metrics_level_ = "detailed";
  std::string metrics_namespace_ = "KinesisProducerLibrary";
  size_t metrics_upload_delay_ = 60000;
  size_t min_connections_ = 1;
  size_t rate_limit_ = 150;
  uint64_t record_max_buffered_time_ = 100;
  uint64_t record_ttl_ = 30000;
  std::string region_ = "";
  uint64_t request_timeout_ = 6000;
  bool verify_certificate_ = true;

  bool use_thread_pool_ = true;
  uint32_t thread_pool_size_ = 64;


  std::vector<std::tuple<std::string, std::string, std::string>>
      additional_metrics_dims_;
};

} //namespace core
} //namespace kinesis
} //namespace aws

#endif //AWS_KINESIS_CORE_CONFIGURATION_H_

