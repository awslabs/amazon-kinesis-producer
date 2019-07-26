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

#ifndef AWS_KINESIS_CORE_TEST_TEST_UTILS_H_
#define AWS_KINESIS_CORE_TEST_TEST_UTILS_H_

#include <unordered_set>

#include <aws/utils/logging.h>

#include <aws/kinesis/core/user_record.h>
#include <aws/kinesis/core/kinesis_record.h>

namespace aws {
namespace kinesis {
namespace test {

using UserRecordSharedPtrVector =
    std::vector<std::shared_ptr<aws::kinesis::core::UserRecord>>;

std::shared_ptr<aws::kinesis::core::UserRecord>
make_user_record(const std::string& partition_key = "abcd",
                 const std::string& data = "1234",
                 const std::string& explicit_hash_key = "",
                 uint64_t deadline = 100000,
                 const std::string& stream = "myStream",
                 uint64_t source_id = 0);

// Create a pipe with mkfifo, deleting it when the Fifo instance is destroyed
class Fifo {
 public:
  Fifo();
  ~Fifo();
  operator const char*() const;
 private:
  std::string name_;
};

// Use only printable chars so we can look at it on the console if tests fail
std::string random_string(size_t len);

void verify_format(const UserRecordSharedPtrVector& original,
                   aws::kinesis::core::KinesisRecord& kr,
                   aws::kinesis::protobuf::AggregatedRecord& container);

void verify_content(const UserRecordSharedPtrVector& original,
                    const aws::kinesis::protobuf::AggregatedRecord& result);

void verify(const UserRecordSharedPtrVector& original,
            aws::kinesis::core::KinesisRecord& kr);

void verify_unaggregated(
    const std::shared_ptr<aws::kinesis::core::UserRecord>& ur,
    aws::kinesis::core::KinesisRecord& kr);

} //namespace test
} //namespace kinesis
} //namespace aws

#endif //AWS_KINESIS_CORE_TEST_TEST_UTILS_H_
