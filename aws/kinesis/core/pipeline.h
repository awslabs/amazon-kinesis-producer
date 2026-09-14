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
#include <atomic>

#include <aws/kinesis/core/aggregator.h>
#include <aws/kinesis/core/collector.h>
#include <aws/kinesis/core/configuration.h>
#include <aws/kinesis/core/ipc_manager.h>
#include <aws/kinesis/core/limiter.h>
#include <aws/kinesis/core/put_records_context.h>
#include <aws/kinesis/core/retrier.h>
#include <aws/kinesis/KinesisClient.h>
#include <aws/kinesis/model/ListShardsRequest.h>
#include <aws/metrics/metrics_manager.h>
#include <aws/utils/processing_statistics_logger.h>

#include <aws/utils/logging.h>


namespace aws {
namespace kinesis {
namespace core {

// A stream's record distribution strategy, as discovered via
// DescribeStreamSummary (or seeded from the customer-configured default).
// UNKNOWN means the strategy has not been determined yet; it is treated like
// AUTO for record handling (no aggregation), which is safe for both stream
// types. A confirmed USER_PARTITION_KEY stream aggregates, as does the
// LEGACY_AGGREGATE backward-compat fallback (below).
enum class StreamStrategy {
  UNKNOWN,
  AUTO,
  USER_PARTITION_KEY,
  // Backward-compat fallback when no default is configured and discovery fails
  // (e.g. the role lacks kinesis:DescribeStreamSummary after a version bump).
  // Aggregates like the pre-AUTO KPL so an upgrade never silently loses
  // aggregation. Unlike USER_PARTITION_KEY it does not reject empty partition
  // keys and is never reported to Java (the real strategy is still unknown);
  // background discovery replaces it once it succeeds.
  LEGACY_AGGREGATE
};

class Pipeline : boost::noncopyable {
 public:
  using Configuration = aws::kinesis::core::Configuration;
  using TimePoint = std::chrono::steady_clock::time_point;
  using StreamIdGetter = std::function<std::string(const std::string&)>;
  using StreamStrategyGetter = std::function<StreamStrategy(const std::string&)>;
  // Test seam: when set, receives the assembled PutRecordsContext instead of
  // dispatching it to the real KinesisClient. Lets tests observe what would be
  // sent (e.g. the solo records produced for AUTO streams) without a live
  // client. Null in production.
  using PutRecordsHandler =
      std::function<void(const std::shared_ptr<PutRecordsContext>&)>;
  // Notifies strategy discovery that a record landed on an unexpected shard.
  // Receives the stream name. Null in production tests that don't care.
  using WrongShardCallback = std::function<void(const std::string&)>;

  Pipeline(
      std::string region,
      std::string stream,
      std::shared_ptr<Configuration> config,
      std::shared_ptr<aws::utils::Executor> executor,
      std::shared_ptr<Aws::Kinesis::KinesisClient> kinesis_client,
      std::shared_ptr<aws::metrics::MetricsManager> metrics_manager,
      Retrier::UserRecordCallback finish_user_record_cb,
      StreamIdGetter stream_id_getter,
      // Defaults to USER_PARTITION_KEY so behavior is unchanged when no
      // DSS-backed getter is supplied. Production and tests inject their own.
      StreamStrategyGetter stream_strategy_getter =
          [](const std::string&) { return StreamStrategy::USER_PARTITION_KEY; },
      // Optional injected ShardMap (tests). When null, the Pipeline creates its
      // own backed by the KinesisClient, as in production.
      std::shared_ptr<ShardMap> shard_map = nullptr,
      // Optional test seam for intercepting the PutRecords send (see
      // PutRecordsHandler). Null in production.
      PutRecordsHandler put_records_handler = nullptr,
      // Notifies strategy discovery of a Wrong Shard event. Null = no-op.
      WrongShardCallback wrong_shard_cb = nullptr)
      : stream_(std::move(stream)),
        region_(std::move(region)),
        stream_arn_(""),
        stream_id_(""),
        stream_id_getter_(std::move(stream_id_getter)),
        stream_strategy_getter_(std::move(stream_strategy_getter)),
        put_records_handler_(std::move(put_records_handler)),
        wrong_shard_cb_(std::move(wrong_shard_cb)),
        config_(std::move(config)),
        stats_logger_(stream_, config_->record_max_buffered_time()),
        executor_(std::move(executor)),
        kinesis_client_(std::move(kinesis_client)),
        metrics_manager_(std::move(metrics_manager)),
        finish_user_record_cb_(std::move(finish_user_record_cb)),
        shard_map_(
            shard_map ? shard_map :
            std::make_shared<ShardMap>(
                executor_,
                [this](auto& req, auto& handler, auto& context) { kinesis_client_->ListShardsAsync(handler, context, req ); },
                stream_,
                stream_arn_,
                stream_id_getter_,
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
                [this](auto& actual_shard) { return shard_map_->hashrange(actual_shard); },
                [this](auto& tp, auto predicted_shard) { shard_map_->invalidate(tp, predicted_shard); },
                [this](auto& code, auto& msg) {
                  limiter_->add_error(code, msg);
                },
                metrics_manager_,
                [this](const std::string& stream_name) {
                  if (wrong_shard_cb_) {
                    wrong_shard_cb_(stream_name);
                  }
                })),
        user_records_rcvd_metric_(
            metrics_manager_
                ->finder()
                .set_name(aws::metrics::constants::Names::UserRecordsReceived)
                .set_stream(stream_)
                .find()),
        outstanding_user_records_(0) {

        if (stream_id_getter_) {
          stream_id_ = stream_id_getter_(stream_);
        }

        if (!stream_id_.empty()) {
          LOG(info) << "Created pipeline for stream \"" << stream_ << "\" with streamId \"" << stream_id_ << "\"";
        } else {
          LOG(info) << "Created pipeline for stream \"" << stream_ << "\"";
        }
  }

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

  void set_stream_id(const std::string& stream_id) {
    stream_id_ = stream_id;
    LOG(debug) << "Set StreamId for stream: " << stream_
                    << ", stream_id: " << stream_id_;
  }

 private:

  void aggregator_put(const std::shared_ptr<UserRecord>& ur) {
    // Sample the strategy exactly once per record. Everything downstream (solo
    // vs aggregated routing here, and request assembly later) keys off this one
    // snapshot via KinesisRecord::service_routed(), so a strategy flip mid-flight
    // can never produce a record that is routed one way but assembled the other.
    StreamStrategy strategy = stream_strategy_getter_(stream_);

    // A null/empty partition key is only valid on an AUTO stream, where the
    // service routes records itself. The Java client rejects it synchronously
    // when it already knows the stream is USER_PARTITION_KEY, but a record
    // submitted while the strategy still looked like AUTO can slip past that
    // check. If discovery later resolves the stream to USER_PARTITION_KEY, such a
    // record must not be allowed to succeed: without this gate a retried empty-PK
    // record would re-enter here, now aggregate (acquiring the aggregated
    // container's synthetic partition key), and land on the stream, violating the
    // invariant that a record with no partition key never lands on a
    // USER_PARTITION_KEY stream. Fail it here instead, matching the Java client's
    // wording.
    if (strategy == StreamStrategy::USER_PARTITION_KEY &&
        ur->partition_key().empty()) {
      auto now = std::chrono::steady_clock::now();
      ur->add_attempt(
          Attempt()
              .set_start(now)
              .set_end(now)
              .set_error("InvalidPartitionKey", "partitionKey cannot be null"));
      finish_user_record(ur);
      return;
    }

    // USER_PARTITION_KEY and the LEGACY_AGGREGATE fallback aggregate; UNKNOWN or
    // AUTO -> solo (the service routes the record). The solo path reuses
    // Aggregator's no-shard branch, which clears predicted_shard so the retrier
    // skips the Wrong Shard comparison.
    bool aggregate = strategy == StreamStrategy::USER_PARTITION_KEY ||
                     strategy == StreamStrategy::LEGACY_AGGREGATE;
    bool service_routed = !aggregate;
    auto kr = aggregator_->put(ur, /*force_solo=*/service_routed);
    if (kr) {
      kr->set_service_routed(service_routed);
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
    // Per-record assembly (service-routed vs not) is decided by each
    // KinesisRecord's service_routed() flag, frozen when the record was routed.
    // We deliberately do not re-sample the stream strategy here: a flip between
    // routing and assembly must not change how an already-routed record is sent.
    auto prc = std::make_shared<PutRecordsContext>(
        stream_, stream_arn_, stream_id_, prr->items());
    prc->set_start(std::chrono::steady_clock::now());
    if (put_records_handler_) {
      // Test seam: hand the context to the injected handler instead of the
      // real client.
      put_records_handler_(prc);
      return;
    }
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

  std::string region_;
  std::string stream_;
  std::string stream_arn_;
  std::string stream_id_;
  StreamIdGetter stream_id_getter_;
  StreamStrategyGetter stream_strategy_getter_;
  PutRecordsHandler put_records_handler_;
  WrongShardCallback wrong_shard_cb_;
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
