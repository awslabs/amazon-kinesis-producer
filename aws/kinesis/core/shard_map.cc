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

#include <thread>
#include <aws/kinesis/core/shard_map.h>

#include <aws/kinesis/model/ListShardsRequest.h>

namespace aws {
namespace kinesis {
namespace core {

const std::chrono::milliseconds ShardMap::kMinBackoff{1000};
const std::chrono::milliseconds ShardMap::kMaxBackoff{30000};
const std::chrono::milliseconds ShardMap::kClosedShardTtl{60000};

ShardMap::ShardMap(
    std::shared_ptr<aws::utils::Executor> executor,
    ListShardsCallBack list_shards_callback,
    std::string stream,
    std::string stream_arn,
    std::string stream_id,
    std::shared_ptr<aws::metrics::MetricsManager> metrics_manager,
    std::chrono::milliseconds min_backoff,
    std::chrono::milliseconds max_backoff,
    std::chrono::milliseconds closed_shard_ttl)
    : executor_(std::move(executor)),
      stream_(std::move(stream)),
      stream_arn_(std::move(stream_arn)),
      stream_id_(std::move(stream_id)),
      metrics_manager_(std::move(metrics_manager)),
      state_(INVALID),
      min_backoff_(min_backoff),
      max_backoff_(max_backoff),
      closed_shard_ttl_(closed_shard_ttl),
      backoff_(min_backoff_),
      list_shards_callback_(list_shards_callback) {
  update();
  std::thread cleanup_thread_(&ShardMap::cleanup, this);
  cleanup_thread_.detach();
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

boost::optional<std::pair<ShardMap::uint128_t, ShardMap::uint128_t>> ShardMap::hashrange(const uint64_t& shard_id) {
  ReadLock lock(shard_cache_mutex_);
  const auto& it = shard_id_to_shard_hashkey_cache_.find(shard_id);
  if (it != shard_id_to_shard_hashkey_cache_.end()) {
      return it->second;
  }
  return boost::none;
}


void ShardMap::invalidate(const TimePoint& seen_at, const boost::optional<uint64_t> predicted_shard) {
  WriteLock lock(mutex_);
  
  if (seen_at > updated_at_ && state_ == READY) {
    if (!predicted_shard || open_shards_.count(*predicted_shard)) {
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
    if (!stream_arn_.empty()) req.SetStreamARN(stream_arn_);
    std::cout << "shard map - stream: " << stream_ << ", stream_id: " << stream_id_ << std::endl;
    // TODO: Uncomment when SDK supports StreamId
    // if (!stream_id_.empty()) req.SetStreamId(stream_id_);
    Aws::Kinesis::Model::ShardFilter shardFilter;
    shardFilter.SetType(Aws::Kinesis::Model::ShardFilterType::AT_LATEST);
    req.SetShardFilter(shardFilter);
  }
  list_shards_callback_(
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

  {
    WriteLock lock(shard_cache_mutex_);
    for (auto& shard : shards) {
      const auto& range = shard.GetHashKeyRange();
      const auto& hashkey_start = uint128_t(range.GetStartingHashKey());
      const auto& hashkey_end = uint128_t(range.GetEndingHashKey());
      const auto& shard_id = shard_id_from_str(shard.GetShardId());
      end_hash_key_to_shard_id_.push_back({hashkey_end, shard_id});
      open_shards_.insert({shard_id, shard});      
      shard_id_to_shard_hashkey_cache_.insert({shard_id, {hashkey_start, hashkey_end}});
    }
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
            << stream_ << (stream_arn_.empty() ? "\"" : "\" (arn: \"" + stream_arn_ + "\"). Found ")
            << end_hash_key_to_shard_id_.size() << " shards";
}

void ShardMap::update_fail(const std::string &code, const std::string &msg) {
  LOG(error) << "Shard map update for stream \""
             << stream_ << (stream_arn_.empty() ? "\"" : "\" (arn: \"" + stream_arn_ + "\") failed. ")
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
  open_shards_.clear();
}

void ShardMap::sort_all_open_shards() {
  std::sort(end_hash_key_to_shard_id_.begin(),
          end_hash_key_to_shard_id_.end());
}

void ShardMap::cleanup() {
  while (true) {
    try {
      std::this_thread::sleep_for(closed_shard_ttl_ / 2); 
      const auto now = std::chrono::steady_clock::now();   
      // readlock on the main mutex and the state_ check ensures that we are not runing list shards so it's safe to
      // clean up the map.
      ReadLock lock(mutex_);
      // if it's been a while since the last shardmap update, we can remove the unused closed shards.
      if (updated_at_ + closed_shard_ttl_ < now && state_ == READY) {
        if (open_shards_.size() != shard_id_to_shard_hashkey_cache_.size()) {
          WriteLock lock(shard_cache_mutex_);
          for (auto it = shard_id_to_shard_hashkey_cache_.begin(); it != shard_id_to_shard_hashkey_cache_.end();) {
            if (open_shards_.count(it->first) == 0) {
              it = shard_id_to_shard_hashkey_cache_.erase(it);
            } else {
              ++it;
            }
          }
        } 
      }
    } catch (const std::exception &e) {
      LOG(error) << "Exception occurred while cleaning up shardmap cache : " << e.what();
    } catch (...) {
      LOG(error) << "Unknown exception while cleaning up shardmap cache.";
    }
  }
}

} //namespace core
} //namespace kinesis
} //namespace aws
