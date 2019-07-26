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

#include <aws/kinesis/core/shard_map.h>

#include <aws/kinesis/model/DescribeStreamRequest.h>

namespace aws {
namespace kinesis {
namespace core {

const std::chrono::milliseconds ShardMap::kMinBackoff{1000};
const std::chrono::milliseconds ShardMap::kMaxBackoff{30000};

ShardMap::ShardMap(
    std::shared_ptr<aws::utils::Executor> executor,
    std::shared_ptr<Aws::Kinesis::KinesisClient> kinesis_client,
    std::string stream,
    std::shared_ptr<aws::metrics::MetricsManager> metrics_manager,
    std::chrono::milliseconds min_backoff,
    std::chrono::milliseconds max_backoff)
    : executor_(std::move(executor)),
      kinesis_client_(std::move(kinesis_client)),
      stream_(std::move(stream)),
      metrics_manager_(std::move(metrics_manager)),
      state_(INVALID),
      min_backoff_(min_backoff),
      max_backoff_(max_backoff),
      backoff_(min_backoff_) {
  update();
}

boost::optional<uint64_t> ShardMap::shard_id(const uint128_t& hash_key) {
  ReadLock lock(mutex_, aws::defer_lock);

  if (lock.try_lock() && state_ == READY) {
    auto it = std::lower_bound(end_hash_key_to_shard_id_.begin(),
                               end_hash_key_to_shard_id_.end(),
                               hash_key,
                               [](const auto& pair, auto key) {
                                 return pair.first < key;
                               });
    if (it != end_hash_key_to_shard_id_.end()) {
      return it->second;
    } else {
      LOG(error) << "Could not map hash key to shard id. Something's wrong"
                 << " with the shard map. Hash key = " << hash_key;
    }
  }

  return boost::none;
}

void ShardMap::invalidate(TimePoint seen_at) {
  WriteLock lock(mutex_);

  if (seen_at > updated_at_ && state_ == READY) {
    update();
  }
}

void ShardMap::update(const std::string& start_shard_id) {
  if (start_shard_id.empty() && state_ == UPDATING) {
    return;
  }

  if (state_ != UPDATING) {
    state_ = UPDATING;
    LOG(info) << "Updating shard map for stream \"" << stream_ << "\"";
    end_hash_key_to_shard_id_.clear();
    if (scheduled_callback_) {
      scheduled_callback_->cancel();
    }
  }

  Aws::Kinesis::Model::DescribeStreamRequest req;
  req.SetStreamName(stream_);
  req.SetLimit(10000);
  if (start_shard_id.size() > 0) {
    req.SetExclusiveStartShardId(start_shard_id);
  }

  kinesis_client_->DescribeStreamAsync(
      req,
      [this](auto /*client*/, auto& /*req*/, auto& outcome, auto& /*ctx*/) {
        this->update_callback(outcome);
      },
      std::shared_ptr<const Aws::Client::AsyncCallerContext>());
}

void ShardMap::update_callback(
      const Aws::Kinesis::Model::DescribeStreamOutcome& outcome) {
  if (!outcome.IsSuccess()) {
    auto e = outcome.GetError();
    update_fail(e.GetExceptionName(), e.GetMessage());
    return;
  }

  auto& description = outcome.GetResult().GetStreamDescription();
  auto& status = description.GetStreamStatus();

  if (status != Aws::Kinesis::Model::StreamStatus::ACTIVE &&
      status != Aws::Kinesis::Model::StreamStatus::UPDATING) {
    update_fail("StreamNotReady");
    return;
  }

  auto& shards = description.GetShards();
  for (auto& shard : shards) {
    // Check if the shard is closed, if so, do not use it.
    if (shard.GetSequenceNumberRange().GetEndingSequenceNumber().size() > 0) {
      continue;
    }
    end_hash_key_to_shard_id_.push_back(
        std::make_pair<uint128_t, uint64_t>(
            uint128_t(shard.GetHashKeyRange().GetEndingHashKey()),
            shard_id_from_str(shard.GetShardId())));
  }

  backoff_ = min_backoff_;

  if (description.GetHasMoreShards()) {
    update(shards[shards.size() - 1].GetShardId());
    return;
  }

  std::sort(end_hash_key_to_shard_id_.begin(),
            end_hash_key_to_shard_id_.end());

  WriteLock lock(mutex_);
  state_ = READY;
  updated_at_ = std::chrono::steady_clock::now();

  LOG(info) << "Successfully updated shard map for stream \""
            << stream_ << "\" found " << end_hash_key_to_shard_id_.size()
            << " shards";
}

void ShardMap::update_fail(const std::string& code, const std::string& msg) {
  LOG(error) << "Shard map update for stream \"" << stream_ << "\" failed. "
             << "Code: " << code << " Message: " << msg << "; retrying in "
             << backoff_.count() << " ms";

  WriteLock lock(mutex_);
  state_ = INVALID;

  if (!scheduled_callback_) {
    scheduled_callback_ =
        executor_->schedule([
            this] { this->update(); },
            backoff_);
  } else {
    scheduled_callback_->reschedule(backoff_);
  }

  backoff_ = std::min(backoff_ * 3 / 2, max_backoff_);
}

} //namespace core
} //namespace kinesis
} //namespace aws
