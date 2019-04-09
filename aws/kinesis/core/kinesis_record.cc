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

#include <aws/kinesis/core/kinesis_record.h>

namespace aws {
namespace kinesis {
namespace core {

namespace detail {

std::pair<bool, uint32_t> KeySet::add(const std::string& s) {
  auto it = lookup_.find(s);
  if (it != lookup_.end()) {
    counts_[s]++;
    return std::pair<bool, uint32_t>(false, it->second);
  } else {
    lookup_.emplace(s, (uint32_t) keys_.size());
    counts_.emplace(s, 1);
    keys_.push_back(s);
    return std::pair<bool, uint32_t>(true, (uint32_t)(keys_.size() - 1));
  }
}

bool KeySet::empty() const {
  return keys_.empty();
}

void KeySet::clear() {
  keys_.clear();
  lookup_.clear();
  counts_.clear();
}

std::pair<bool, uint32_t> KeySet::remove_one(const std::string& s) {
  auto cnt_it = counts_.find(s);
  if (cnt_it->second > 1) {
    (cnt_it->second)--;
    return std::pair<bool, uint32_t>(false, 0);
  } else {
    counts_.erase(cnt_it);
    auto idx = lookup_[s];
    keys_.erase(keys_.begin() + idx);
    lookup_.erase(s);
    for (auto& p : lookup_) {
      if (p.second > idx) {
        p.second--;
      }
    }
    return std::pair<bool, uint32_t>(true, idx);
  }
}

const std::string& KeySet::first() const {
  return keys_[0];
}

} //namespace detail

KinesisRecord::KinesisRecord()
    : estimated_size_(0),
      cached_accurate_size_(0),
      cached_accurate_size_valid_(false) {}

size_t KinesisRecord::accurate_size() {
  if (items_.size() == 0) {
    return 0;
  } else if (items_.size() == 1) {
    return items_.front()->data().length();
  } else {
    if (!cached_accurate_size_valid_) {
      cached_accurate_size_valid_ = true;
      cached_accurate_size_ = kFixedOverhead + aggregated_record_.ByteSize();
    }
    return cached_accurate_size_;
  }
}

size_t KinesisRecord::estimated_size() {
  if (items_.size() < 2) {
    return accurate_size();
  } else {
    return estimated_size_;
  }
}

std::string KinesisRecord::serialize() {
  if (items_.empty()) {
    throw std::runtime_error("Cannot serialize empty container");
  }

  if (items_.size() == 1) {
    return items_.front()->data();
  }

  std::string s;
  aggregated_record_.SerializeToString(&s);

  std::string result;
  result.reserve(kFixedOverhead + s.length());
  result += kMagic;
  result += s;
  result += aws::utils::md5(s);
  return result;
}

std::string KinesisRecord::partition_key() const {
  if (items_.empty()) {
    throw std::runtime_error(
        "Cannot compute partition_key for empty container");
  }

  if (items_.size() == 1) {
    return items_.front()->partition_key();
  }

  // We will always set an explicit hash key if we created an aggregated record.
  // We therefore have no need to set a partition key since the records within
  // the container have their own parition keys anyway. We will therefore use a
  // single byte to save space.
  return "a";
}

std::string KinesisRecord::explicit_hash_key() const {
  if (items_.empty()) {
    throw std::runtime_error(
        "Cannot compute explicit_hash_key for empty container");
  }

  return items_.front()->hash_key_decimal_str();
}

void KinesisRecord::after_add(const std::shared_ptr<UserRecord>& ur) {
  cached_accurate_size_valid_ = false;

  auto new_record = aggregated_record_.add_records();
  new_record->set_data(ur->data());
  estimated_size_ += ur->data().length() + 3;

  {
    auto& pk = ur->partition_key();
    auto add_result = partition_keys_.add(pk);
    if (add_result.first) {
      aggregated_record_.add_partition_key_table(pk);
      estimated_size_ += pk.length() + 3;
    }
    new_record->set_partition_key_index(add_result.second);
    estimated_size_ += 2;
  }

  auto ehk = ur->explicit_hash_key();
  if (ehk) {
    auto add_result = explicit_hash_keys_.add(*ehk);
    if (add_result.first) {
      aggregated_record_.add_explicit_hash_key_table(*ehk);
      estimated_size_ += ehk->length() + 3;
    }
    new_record->set_explicit_hash_key_index(add_result.second);
    estimated_size_ += 2;
  }
}

void KinesisRecord::after_remove(const std::shared_ptr<UserRecord>& ur) {
  cached_accurate_size_valid_ = false;

  aggregated_record_.mutable_records()->RemoveLast();
  estimated_size_ -= ur->data().length() + 3;

  {
    auto pk_rm_result = partition_keys_.remove_one(ur->partition_key());
    if (pk_rm_result.first) {
      aggregated_record_.mutable_partition_key_table()->RemoveLast();
      for (int i = 0; i < aggregated_record_.records_size(); i++) {
        auto r = aggregated_record_.mutable_records(i);
        if (r->partition_key_index() > pk_rm_result.second) {
          r->set_partition_key_index(r->partition_key_index() - 1);
        }
      }
      estimated_size_ -= ur->partition_key().length() + 3;
    }
    estimated_size_ -= 2;
  }

  auto ehk = ur->explicit_hash_key();
  if (ehk) {
    auto ehk_rm_result = explicit_hash_keys_.remove_one(ehk.get());
    if (ehk_rm_result.first) {
      aggregated_record_.mutable_explicit_hash_key_table()->RemoveLast();
      for (int i = 0; i < aggregated_record_.records_size(); i++) {
        auto r = aggregated_record_.mutable_records(i);
        if (r->explicit_hash_key_index() > ehk_rm_result.second) {
          r->set_explicit_hash_key_index(r->explicit_hash_key_index() - 1);
        }
      }
      estimated_size_ -= ehk->length() + 3;
    }
    estimated_size_ -= 2;
  }
}

void KinesisRecord::after_clear() {
  cached_accurate_size_valid_ = false;
  explicit_hash_keys_.clear();
  partition_keys_.clear();
  aggregated_record_.mutable_records()->Clear();
  aggregated_record_.mutable_explicit_hash_key_table()->Clear();
  aggregated_record_.mutable_partition_key_table()->Clear();
  estimated_size_ = 0;
}

} //namespace core
} //namespace kinesis
} //namespace aws
