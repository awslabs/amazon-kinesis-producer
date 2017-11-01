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

#ifndef AWS_KINESIS_CORE_KINESIS_RECORD_H_
#define AWS_KINESIS_CORE_KINESIS_RECORD_H_

#include <unordered_map>

#include "messages.pb.h"
#include <aws/kinesis/core/serializable_container.h>
#include <aws/kinesis/core/user_record.h>

namespace aws {
namespace kinesis {
namespace core {

namespace detail {

class KeySet {
 public:
  std::pair<bool, uint32_t> add(const std::string& s);
  bool empty() const;
  void clear();
  std::pair<bool, uint32_t> remove_one(const std::string& d);
  const std::string& first() const;

 private:
  std::vector<std::string> keys_;
  std::unordered_map<std::string, uint32_t> lookup_;
  std::unordered_map<std::string, size_t> counts_;
};

} // namespace detail

class KinesisRecord : public SerializableContainer<UserRecord> {
 public:
  static constexpr const char* kMagic = "\xF3\x89\x9A\xC2";

  KinesisRecord();

  size_t accurate_size() override;
  size_t estimated_size() override;

  std::string serialize() override;

  std::string partition_key() const;
  std::string explicit_hash_key() const;

 protected:
  void after_add(const std::shared_ptr<UserRecord>& ur) override;
  void after_remove(const std::shared_ptr<UserRecord>& ur) override;
  void after_clear() override;

 private:
  static const size_t kFixedOverhead = 4 + 16;

  aws::kinesis::protobuf::AggregatedRecord aggregated_record_;
  detail::KeySet explicit_hash_keys_;
  detail::KeySet partition_keys_;
  size_t estimated_size_;
  size_t cached_accurate_size_;
  bool cached_accurate_size_valid_;
};

} //namespace core
} //namespace kinesis
} //namespace aws

#endif //AWS_KINESIS_CORE_KINESIS_RECORD_H_
