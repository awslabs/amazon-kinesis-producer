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

#ifndef AWS_KINESIS_CORE_PUT_RECORDS_REQUEST_H_
#define AWS_KINESIS_CORE_PUT_RECORDS_REQUEST_H_

#include <aws/kinesis/core/serializable_container.h>
#include <aws/kinesis/core/kinesis_record.h>
#include <aws/utils/utils.h>

namespace aws {
namespace kinesis {
namespace core {

class PutRecordsRequest : public SerializableContainer<KinesisRecord> {
 public:
  PutRecordsRequest() : total_size_(0) {}

  size_t accurate_size() override {
    return total_size_;
  }

  std::string serialize() override {
    if (items_.empty()) {
      throw std::runtime_error("Cannot serialize an empty PutRecordsRequest");
    }

    std::string s;
    // The data will expand due to Base64 and json overhead
    s.reserve(accurate_size() * 2);

    s += "{\"StreamName\":\"";
    s += stream();
    s += "\",\"Records\":[";
    for (const auto& kr : items_) {
      s += "{\"Data\":\"";
      s += aws::utils::base64_encode(kr->serialize());
      s += "\",\"PartitionKey\":\"";
      s += kr->partition_key();
      s += "\",\"ExplicitHashKey\":\"";
      s += kr->explicit_hash_key();
      s += "\"},";
    }
    s.back() = ']';
    s += '}';

    return s;
  }

  const std::string& stream() {
    if (items_.empty()) {
      throw std::runtime_error(
          "Cannot get stream name of an empty PutRecordsRequest");
    }
    return items_.at(0)->items().at(0)->stream();
  }

 protected:
  void after_add(const std::shared_ptr<KinesisRecord>& ur) override {
    // This is how the backend counts towards the current 5MB limit
    total_size_ += ur->partition_key().length() + ur->accurate_size();
  }

  void after_remove(const std::shared_ptr<KinesisRecord>& ur) override {
    total_size_ -= ur->partition_key().length() + ur->accurate_size();
  }

  void after_clear() override {
    total_size_ = 0;
  }

 private:
  size_t total_size_;
};

} //namespace core
} //namespace kinesis
} //namespace aws

#endif //AWS_KINESIS_CORE_PUT_RECORDS_REQUEST_H_
