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

#ifndef AWS_KINESIS_CORE_STREAM_STRATEGY_MANAGER_H_
#define AWS_KINESIS_CORE_STREAM_STRATEGY_MANAGER_H_

#include <chrono>
#include <functional>
#include <string>
#include <unordered_map>

#include <boost/optional.hpp>

#include <aws/kinesis/core/pipeline.h>  // for StreamStrategy
#include <aws/mutex.h>
#include <aws/utils/executor.h>

namespace aws {
namespace kinesis {
namespace core {

// Wire values for RecordDistributionStrategy, shared by the proto string fields
// (StreamMetadata, StreamStrategyUpdate, config default) and the service API.
constexpr const char* kStrategyAuto = "AUTO";
constexpr const char* kStrategyUserPartitionKey = "USER_PARTITION_KEY";

// Maps a wire string to a StreamStrategy. Unrecognized or empty -> UNKNOWN.
inline StreamStrategy strategy_from_string(const std::string& s) {
  if (s == kStrategyAuto) {
    return StreamStrategy::AUTO;
  }
  if (s == kStrategyUserPartitionKey) {
    return StreamStrategy::USER_PARTITION_KEY;
  }
  return StreamStrategy::UNKNOWN;
}

// Maps a StreamStrategy to its wire string. UNKNOWN and the internal
// LEGACY_AGGREGATE fallback (never sent) -> empty string.
inline std::string strategy_to_string(StreamStrategy strategy) {
  switch (strategy) {
    case StreamStrategy::AUTO:
      return kStrategyAuto;
    case StreamStrategy::USER_PARTITION_KEY:
      return kStrategyUserPartitionKey;
    case StreamStrategy::UNKNOWN:
    case StreamStrategy::LEGACY_AGGREGATE:
      break;
  }
  return "";
}

// True for a real DescribeStreamSummary result, false for the pre-discovery
// UNKNOWN state and the LEGACY_AGGREGATE fallback (both "not yet discovered").
// Lets background recovery tell whether discovery still needs to make progress.
inline bool is_discovered_strategy(StreamStrategy s) {
  return s == StreamStrategy::AUTO || s == StreamStrategy::USER_PARTITION_KEY;
}

// Discovers and caches each stream's RecordDistributionStrategy, and notifies a
// listener (Java, via StreamStrategyUpdate) when a stream's strategy is first
// resolved or later changes.
//
// Behavior:
//   - If a customer default is configured, every stream starts at that default
//     and discovery only corrects drift in the background.
//   - With no default, a stream starts UNKNOWN. The first write triggers a
//     bounded blocking discovery (a few attempts with backoff); on success the
//     strategy is cached (records flow solo while UNKNOWN, which is safe). On
//     failure the stream falls back to LEGACY_AGGREGATE -- it keeps aggregating
//     like the pre-AUTO KPL, so a version bump lacking the DescribeStreamSummary
//     permission doesn't silently drop aggregation -- and a background recovery
//     is scheduled to replace it with the real strategy.
//   - A periodic refresh re-checks each stream for drift.
//   - A reactive Wrong Shard trigger forces an immediate re-check when a stream
//     accumulates consecutive Wrong Shard retries (a hint the strategy changed).
//
// Timing knobs for StreamStrategyManager. Defined at namespace scope (not nested)
// so it is a complete type usable as a default argument in the manager's
// constructor.
struct StreamStrategyTiming {
  // Blocking first-write discovery (no default configured).
  int blocking_max_attempts = 3;
  std::chrono::milliseconds blocking_backoff{1000};
  // Background recovery after a failed blocking discovery.
  std::vector<std::chrono::milliseconds> recovery_backoff{
      std::chrono::milliseconds(5000),
      std::chrono::milliseconds(15000),
      std::chrono::milliseconds(60000)};
  // Steady-state drift refresh.
  std::chrono::milliseconds refresh_interval{300000};
};

// The actual DescribeStreamSummary call is injected as a StrategyResolver so the
// SDK dependency (which does not yet expose RecordDistributionStrategy) stays
// out of this component and out of its tests.
class StreamStrategyManager : boost::noncopyable {
 public:
  using Timing = StreamStrategyTiming;

  // Resolves a stream's strategy, typically by calling DescribeStreamSummary.
  // Returns the strategy on success, or boost::none on failure (throttle,
  // error, or the field being unavailable). Must be safe to call from the
  // executor and from a caller thread.
  using StrategyResolver =
      std::function<boost::optional<StreamStrategy>(const std::string&)>;

  // Invoked when a stream's strategy is first resolved or changes. Wired to send
  // a StreamStrategyUpdate to Java.
  using StrategyChangeCallback =
      std::function<void(const std::string&, StreamStrategy)>;

  // Consecutive Wrong Shard retries that trigger an immediate re-check.
  static constexpr int kWrongShardThreshold = 3;

  StreamStrategyManager(std::shared_ptr<aws::utils::Executor> executor,
                        StreamStrategy default_strategy,
                        StrategyResolver resolver,
                        StrategyChangeCallback on_change,
                        Timing timing = Timing());

  // Non-blocking read of a stream's current strategy. Returns the default (or
  // UNKNOWN) if the stream has not been discovered yet.
  StreamStrategy get_strategy(const std::string& stream) const;

  // Called on a stream's first write. If a default is configured the strategy is
  // already known and this returns immediately. With no default and an
  // undiscovered stream, runs a bounded blocking discovery and returns the
  // resolved strategy (or LEGACY_AGGREGATE on failure, scheduling background
  // recovery). Idempotent and safe under concurrent first writes for the same
  // stream.
  StreamStrategy get_or_discover(const std::string& stream);

  // Records a Wrong Shard retry for a stream. After kWrongShardThreshold
  // occurrences, triggers an immediate re-discovery. The counter is reset when
  // a discovery re-check completes (see apply_resolved), so the threshold
  // counts hits since the last confirmed strategy.
  void record_wrong_shard(const std::string& stream);

  // Performs one (synchronous) discovery attempt for a stream and applies the
  // result: updates the cache and fires on_change if the strategy changed.
  // Exposed for the scheduled refresh/recovery tasks and for tests.
  void refresh_one(const std::string& stream);

 private:
  enum class DiscoveryState { kNotStarted, kInProgress, kDone };

  struct Entry {
    // Must be initialized: record_wrong_shard can insert an Entry (via
    // streams_[stream]) before discovery runs, and a subsequent get_strategy
    // would otherwise read an indeterminate enum.
    StreamStrategy strategy = StreamStrategy::UNKNOWN;
    DiscoveryState discovery = DiscoveryState::kNotStarted;
    int wrong_shard_count = 0;
    // True while a Wrong-Shard-triggered re-check (refresh_one) is in flight for
    // this stream. Prevents a burst of near-simultaneous Wrong Shard retries from
    // each submitting its own DescribeStreamSummary: during the ~hundreds of ms a
    // re-check takes, dozens of retries can trip the threshold and would otherwise
    // each fire a redundant DSS call (measured ~40 calls for one correction). With
    // this guard only the first submits; the rest are suppressed until it returns.
    bool recheck_in_flight = false;
  };

  // Applies a resolved strategy: updates the cache and fires on_change if it
  // differs from the current value. Returns the (possibly unchanged) strategy.
  StreamStrategy apply_resolved(const std::string& stream,
                                StreamStrategy resolved);

  void schedule_recovery(const std::string& stream, size_t attempt);
  void schedule_refresh(const std::string& stream);

  std::shared_ptr<aws::utils::Executor> executor_;
  StreamStrategy default_strategy_;
  StrategyResolver resolver_;
  StrategyChangeCallback on_change_;
  Timing timing_;

  mutable aws::shared_mutex mutex_;
  std::unordered_map<std::string, Entry> streams_;
};

} //namespace core
} //namespace kinesis
} //namespace aws

#endif //AWS_KINESIS_CORE_STREAM_STRATEGY_MANAGER_H_
