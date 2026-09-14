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

#include <aws/kinesis/core/stream_strategy_manager.h>

#include <aws/utils/logging.h>
#include <aws/utils/utils.h>

namespace aws {
namespace kinesis {
namespace core {

constexpr int StreamStrategyManager::kWrongShardThreshold;

StreamStrategyManager::StreamStrategyManager(
    std::shared_ptr<aws::utils::Executor> executor,
    StreamStrategy default_strategy,
    StrategyResolver resolver,
    StrategyChangeCallback on_change,
    Timing timing)
    : executor_(std::move(executor)),
      default_strategy_(default_strategy),
      resolver_(std::move(resolver)),
      on_change_(std::move(on_change)),
      timing_(std::move(timing)) {}

StreamStrategy StreamStrategyManager::get_strategy(
    const std::string& stream) const {
  aws::shared_lock<aws::shared_mutex> lock(mutex_);
  auto it = streams_.find(stream);
  if (it != streams_.end()) {
    return it->second.strategy;
  }
  return default_strategy_;
}

StreamStrategy StreamStrategyManager::get_or_discover(
    const std::string& stream) {
  // Fast path: a default is configured, or the stream is already discovered /
  // discovery is already underway. Never block in these cases.
  {
    aws::unique_lock<aws::shared_mutex> lock(mutex_);
    auto& entry = streams_[stream];
    if (default_strategy_ != StreamStrategy::UNKNOWN) {
      // Seed from the default once; background refresh corrects drift.
      if (entry.discovery == DiscoveryState::kNotStarted) {
        entry.strategy = default_strategy_;
        entry.discovery = DiscoveryState::kInProgress;
        // Kick off async discovery to detect drift from the default.
        schedule_refresh(stream);
      }
      return entry.strategy;
    }
    if (entry.discovery != DiscoveryState::kNotStarted) {
      // Another first-write already started (or finished) discovery.
      return entry.strategy;
    }
    // No default and not yet discovered: claim the blocking discovery.
    entry.discovery = DiscoveryState::kInProgress;
    entry.strategy = StreamStrategy::UNKNOWN;
  }

  // Bounded blocking discovery (no default case). Records are held by the caller
  // (the daemon) until this returns.
  for (int attempt = 0; attempt < timing_.blocking_max_attempts; attempt++) {
    auto resolved = resolver_(stream);
    if (resolved) {
      auto strategy = apply_resolved(stream, *resolved);
      // Arm the steady-state drift refresh. Without this, a stream that resolves
      // on its first write (the common no-default path) would never be
      // re-checked, so a later strategy change would go undetected.
      schedule_refresh(stream);
      return strategy;
    }
    if (attempt + 1 < timing_.blocking_max_attempts) {
      aws::utils::sleep_for(timing_.blocking_backoff);
    }
  }

  // All blocking attempts failed and the strategy is unknown (no default). Solo
  // would silently drop aggregation on a version bump (the pre-AUTO KPL always
  // aggregated) -- typically when the role lacks kinesis:DescribeStreamSummary.
  // Fall back to LEGACY_AGGREGATE: keep aggregating while background recovery
  // discovers the real strategy. No-default case only; an explicit default is
  // seeded up front and never reaches here.
  LOG(warning) << "Could not determine RecordDistributionStrategy for stream \""
               << stream << "\" after " << timing_.blocking_max_attempts
               << " attempts (check that the KPL role is granted "
               << "kinesis:DescribeStreamSummary); falling back to aggregation "
               << "for backward compatibility until discovery succeeds.";
  {
    aws::unique_lock<aws::shared_mutex> lock(mutex_);
    auto& entry = streams_[stream];
    // No on_change_: the fallback is not confirmed, so Java is not told (it must
    // not start rejecting empty PKs as it would for a real USER_PARTITION_KEY).
    entry.strategy = StreamStrategy::LEGACY_AGGREGATE;
    entry.discovery = DiscoveryState::kDone;
  }
  schedule_recovery(stream, 0);
  return StreamStrategy::LEGACY_AGGREGATE;
}

void StreamStrategyManager::record_wrong_shard(const std::string& stream) {
  bool trigger = false;
  {
    aws::unique_lock<aws::shared_mutex> lock(mutex_);
    auto& entry = streams_[stream];
    if (++entry.wrong_shard_count >= kWrongShardThreshold) {
      entry.wrong_shard_count = 0;
      // Only submit a re-check if one is not already in flight for this stream.
      // A burst of Wrong Shard retries (e.g. a wrong USER_PARTITION_KEY default on
      // an AUTO stream) trips the threshold repeatedly while the first re-check's
      // DescribeStreamSummary is still outstanding; without this guard each one
      // fires its own redundant DSS call.
      if (!entry.recheck_in_flight) {
        entry.recheck_in_flight = true;
        trigger = true;
      }
    }
  }
  if (trigger) {
    LOG(info) << "Stream \"" << stream << "\" hit " << kWrongShardThreshold
              << " Wrong Shard retries; re-checking its "
              << "RecordDistributionStrategy.";
    auto self_stream = stream;
    executor_->submit([this, self_stream]() noexcept {
      refresh_one(self_stream);
      // Re-check done (resolved or failed): allow the next one to be submitted.
      aws::unique_lock<aws::shared_mutex> lock(mutex_);
      streams_[self_stream].recheck_in_flight = false;
    });
  }
}

void StreamStrategyManager::refresh_one(const std::string& stream) {
  auto resolved = resolver_(stream);
  if (resolved) {
    apply_resolved(stream, *resolved);
  }
  // On failure, leave the cached strategy as-is; the next scheduled refresh or
  // recovery attempt will try again.
}

StreamStrategy StreamStrategyManager::apply_resolved(const std::string& stream,
                                                     StreamStrategy resolved) {
  bool changed = false;
  {
    aws::unique_lock<aws::shared_mutex> lock(mutex_);
    auto& entry = streams_[stream];
    if (entry.discovery != DiscoveryState::kDone ||
        entry.strategy != resolved) {
      changed = entry.strategy != resolved;
      entry.strategy = resolved;
    }
    entry.discovery = DiscoveryState::kDone;
    // A completed re-check is the reset point for the Wrong Shard heuristic: the
    // strategy is now confirmed, so any accumulated count is stale.
    entry.wrong_shard_count = 0;
  }
  if (changed && on_change_) {
    on_change_(stream, resolved);
  }
  return resolved;
}

void StreamStrategyManager::schedule_recovery(const std::string& stream,
                                              size_t attempt) {
  if (attempt >= timing_.recovery_backoff.size()) {
    // Recovery backoff exhausted; fall back to the steady-state refresh.
    schedule_refresh(stream);
    return;
  }
  auto delay = timing_.recovery_backoff[attempt];
  auto self_stream = stream;
  executor_->schedule(
      [this, self_stream, attempt]() noexcept {
        auto before = get_strategy(self_stream);
        refresh_one(self_stream);
        if (!is_discovered_strategy(get_strategy(self_stream)) &&
            !is_discovered_strategy(before)) {
          // Still unresolved (UNKNOWN or the LEGACY_AGGREGATE fallback): try the
          // next, longer backoff rather than dropping to the steady refresh.
          schedule_recovery(self_stream, attempt + 1);
        } else {
          // Resolved to a real strategy; resume steady-state drift checks.
          schedule_refresh(self_stream);
        }
      },
      delay);
}

void StreamStrategyManager::schedule_refresh(const std::string& stream) {
  auto self_stream = stream;
  executor_->schedule(
      [this, self_stream]() noexcept {
        refresh_one(self_stream);
        schedule_refresh(self_stream);
      },
      timing_.refresh_interval);
}

} //namespace core
} //namespace kinesis
} //namespace aws
