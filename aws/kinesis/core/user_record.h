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

#ifndef AWS_KINESIS_CORE_USER_RECORD_H_
#define AWS_KINESIS_CORE_USER_RECORD_H_

#include <sstream>
#include <vector>

#include <boost/multiprecision/cpp_int.hpp>
#include <boost/optional.hpp>

#include <aws/kinesis/core/attempt.h>
#include <aws/kinesis/protobuf/messages.pb.h>
#include <aws/utils/time_sensitive.h>
#include <aws/utils/utils.h>

namespace aws {
namespace kinesis {
namespace core {

class UserRecord : public aws::utils::TimeSensitive {
 public:
  using uint128_t = boost::multiprecision::uint128_t;

  // This will move strings out of m; m will not be valid after this.
  UserRecord(aws::kinesis::protobuf::Message& m);

  void add_attempt(const Attempt& a) {
    attempts_.push_back(a);
  }

  uint64_t source_id() const noexcept {
    return source_id_;
  }

  const std::string& stream() const noexcept {
    return stream_;
  }

  const std::string& partition_key() const noexcept {
    return partition_key_;
  }

  const uint128_t& hash_key() const noexcept {
    return hash_key_;
  }

  const std::string& data() const noexcept {
    return data_;
  }

  const std::vector<Attempt>& attempts() const noexcept {
    return attempts_;
  }

  const bool finished() const noexcept {
    return finished_;
  }

  boost::optional<uint64_t> predicted_shard() const noexcept {
    return predicted_shard_;
  }

  void predicted_shard(uint64_t sid) noexcept {
    predicted_shard_ = sid;
  }

  std::string hash_key_decimal_str() const noexcept {
    std::stringstream ss;
    ss << hash_key_;
    return ss.str();
  }

  boost::optional<std::string> explicit_hash_key() const noexcept {
    if (has_explicit_hash_key_) {
      return hash_key_decimal_str();
    } else {
      return boost::none;
    }
  }

  // This will move strings from this instance into the Message. This instance
  // will not be valid after this.
  aws::kinesis::protobuf::Message to_put_record_result();

 private:
  uint64_t source_id_;
  std::string stream_;
  std::string partition_key_;
  uint128_t hash_key_;
  std::string data_;
  std::vector<Attempt> attempts_;
  boost::optional<uint64_t> predicted_shard_;
  bool has_explicit_hash_key_;
  bool finished_;
};

} //namespace core
} //namespace kinesis
} //namespace aws

#endif //AWS_KINESIS_CORE_USER_RECORD_H_
