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

#include <aws/kinesis/model/ListShardsRequest.h>

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



void ShardMap::invalidate(const TimePoint& seen_at, const boost::optional<uint64_t> predicted_shard) {
  WriteLock lock(mutex_);
  
  if (seen_at > updated_at_ && state_ == READY) {
    if (!predicted_shard || std::binary_search(open_shard_ids_.begin(), open_shard_ids_.end(), *predicted_shard)) {
      std::chrono::duration<double, std::milli> fp_ms = seen_at - updated_at_;
      LOG(info) << "Deciding to update shard map for \"" << stream_ 
                <<"\" with a gap between seen_at and updated_at_ of " << fp_ms.count() << " ms " << "predicted shard: " << predicted_shard;
      update();
    }
  }
}

void ShardMap::update() {
  if (state_ == UPDATING) {
    return;
  }

  state_ = UPDATING;
  LOG(info) << "Updating shard map for stream \"" << stream_ << "\"";
  clear_all_stored_shards();
  if (scheduled_callback_) {
    scheduled_callback_->cancel();
  }
  
  //We can call list shards directly without checking for stream state
  //since list shard fails if the stream is not in the appropriate state. 
  list_shards();
}

void ShardMap::list_shards(const Aws::String& next_token) {
  Aws::Kinesis::Model::ListShardsRequest req;
  req.SetMaxResults(1000);

  if (!next_token.empty()) {
    req.SetNextToken(next_token);
  } else {
    req.SetStreamName(stream_);
    Aws::Kinesis::Model::ShardFilter shardFilter;
    shardFilter.SetType(Aws::Kinesis::Model::ShardFilterType::AT_LATEST);
    req.SetShardFilter(shardFilter);
  }

  kinesis_client_->ListShardsAsync(
      req,
      [this](auto /*client*/, auto& /*req*/, auto& outcome, auto& /*ctx*/) {
        this->list_shards_callback(outcome);
      },
      std::shared_ptr<const Aws::Client::AsyncCallerContext>());
}

void ShardMap::list_shards_callback(
      const Aws::Kinesis::Model::ListShardsOutcome& outcome) {
  if (!outcome.IsSuccess()) {
    auto e = outcome.GetError();
    update_fail(e.GetExceptionName(), e.GetMessage());
    return;
  }

  auto& shards = outcome.GetResult().GetShards();  
  for (auto& shard : shards) {
    // We use shard filter for server end to filter out closed shards
    store_open_shard(shard_id_from_str(shard.GetShardId()), 
      uint128_t(shard.GetHashKeyRange().GetEndingHashKey()));
  }

  backoff_ = min_backoff_;
  
  auto& next_token = outcome.GetResult().GetNextToken();
  if (!next_token.empty()) {
    list_shards(next_token);
    return;
  }

  sort_all_open_shards();

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


void ShardMap::clear_all_stored_shards() {
  end_hash_key_to_shard_id_.clear();
  open_shard_ids_.clear();
}

void ShardMap::store_open_shard(const uint64_t shard_id, const uint128_t end_hash_key) {
  end_hash_key_to_shard_id_.push_back(
      std::make_pair(end_hash_key, shard_id));
  open_shard_ids_.push_back(shard_id);
}

void ShardMap::sort_all_open_shards() {
  std::sort(end_hash_key_to_shard_id_.begin(),
          end_hash_key_to_shard_id_.end());
  std::sort(open_shard_ids_.begin(), open_shard_ids_.end());
}


} //namespace core
} //namespace kinesis
} //namespace aws
