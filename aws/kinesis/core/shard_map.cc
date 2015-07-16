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

#include <aws/kinesis/core/shard_map.h>

#include <aws/utils/json.h>

namespace aws {
namespace kinesis {
namespace core {

const std::chrono::milliseconds ShardMap::kMinBackoff{1000};
const std::chrono::milliseconds ShardMap::kMaxBackoff{30000};

ShardMap::ShardMap(
    const std::shared_ptr<aws::utils::Executor>& executor,
    const std::shared_ptr<aws::http::HttpClient>& http_client,
    const std::shared_ptr<aws::auth::AwsCredentialsProvider>& creds,
    const std::string& region,
    const std::string& stream,
    const std::shared_ptr<aws::metrics::MetricsManager>& metrics_manager,
    std::chrono::milliseconds min_backoff,
    std::chrono::milliseconds max_backoff)
    : executor_(executor),
      http_client_(http_client),
      creds_provider_(creds),
      region_(region),
      stream_(stream),
      metrics_manager_(metrics_manager),
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

  std::stringstream ss;
  ss << "{\"StreamName\":\"" << stream_ << "\"";
  if (!start_shard_id.empty()) {
    ss << ",\"ExclusiveStartShardId\":\"" << start_shard_id << "\"";
  }
  ss << "}";

  aws::http::HttpRequest req =
      aws::http::create_kinesis_request(region_,
                                        "DescribeStream",
                                        ss.str());
  aws::auth::SigV4Context ctx(region_, "kinesis", creds_provider_);

  try {
    aws::auth::sign_v4(req, ctx);
    http_client_->put(req, [this](const auto& r) {
      this->update_callback(r);
    });
  } catch (const std::exception& e) {
    update_callback(std::make_shared<aws::http::HttpResult>(e.what()));
  }
}

void ShardMap::update_callback(
    const std::shared_ptr<aws::http::HttpResult>& result) {
  if (!*result) {
    update_fail(result->error());
    return;
  }

  if (result->status_code() != 200) {
    update_fail(result->response_body());
    return;
  }

  try {
    auto json = aws::utils::Json(result->response_body());
    auto stream_description = json["StreamDescription"];

    std::string stream_status = stream_description["StreamStatus"];
    if (stream_status != "ACTIVE" && stream_status != "UPDATING") {
      std::stringstream ss;
      ss << "Stream status is " << stream_status;
      throw std::runtime_error(ss.str().c_str());
    }

    auto shards = stream_description["Shards"];
    for (size_t i = 0; i < shards.size(); i++) {
      // Check if the shard is closed, if so, do not use it.
      if (shards[i]["SequenceNumberRange"]["EndingSequenceNumber"]) {
        continue;
      }
      end_hash_key_to_shard_id_.push_back(
          std::make_pair<uint128_t, uint64_t>(
              uint128_t(
                  (std::string) shards[i]["HashKeyRange"]["EndingHashKey"]),
              shard_id_from_str(shards[i]["ShardId"])));
    }

    backoff_ = min_backoff_;

    if (stream_description["HasMoreShards"]) {
      update(shards[shards.size() - 1]["ShardId"]);
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
  } catch (const std::exception& ex) {
    update_fail(ex.what());
  }
}

void ShardMap::update_fail(const std::string& error) {
  LOG(error) << "Shard map update for stream \"" << stream_ << "\" failed: "
             << error << "; retrying in " << backoff_.count() << " ms";

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
