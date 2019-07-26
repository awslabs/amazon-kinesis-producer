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

#ifndef AWS_KINESIS_CORE_PUT_RECORDS_CONTEXT_H_
#define AWS_KINESIS_CORE_PUT_RECORDS_CONTEXT_H_

#include <memory>
#include <vector>

#include <aws/core/client/AsyncCallerContext.h>
#include <aws/core/utils/Outcome.h>
#include <aws/kinesis/KinesisClient.h>
#include <aws/kinesis/model/PutRecordsRequest.h>
#include <aws/kinesis/model/PutRecordsRequestEntry.h>
#include <aws/kinesis/model/PutRecordsResult.h>
#include <aws/kinesis/core/kinesis_record.h>

namespace aws {
namespace kinesis {
namespace core {

class PutRecordsContext : public Aws::Client::AsyncCallerContext {
 public:
  PutRecordsContext(std::string stream,
                    std::vector<std::shared_ptr<KinesisRecord>> records)
      : stream_(stream),
        records_(std::move(records)) {}

  const std::string& get_stream() const {
    return stream_;
  }

  std::chrono::steady_clock::time_point get_start() const {
    return start_;
  }

  std::chrono::steady_clock::time_point get_end() const {
    return end_;
  }

  size_t duration_millis() const {
    return
        std::chrono::duration_cast<std::chrono::milliseconds>(
            end_ - start_).count();
  }

  const std::vector<std::shared_ptr<KinesisRecord>>& get_records() const {
    return records_;
  }

  const Aws::Kinesis::Model::PutRecordsOutcome& get_outcome() const {
    return outcome_;
  }

  Aws::Kinesis::Model::PutRecordsRequest to_sdk_request() const {
    Aws::Kinesis::Model::PutRecordsRequest req;
    for (auto& kr : records_) {
      auto serialized = kr->serialize();
      Aws::Kinesis::Model::PutRecordsRequestEntry e;
      e.SetData(Aws::Utils::ByteBuffer((const unsigned char*) serialized.data(),
                                       serialized.size()));
      e.SetPartitionKey(kr->partition_key());
      e.SetExplicitHashKey(kr->explicit_hash_key());
      req.AddRecords(std::move(e));
    }
    req.SetStreamName(stream_);
    return req;
  }

  PutRecordsContext& set_start(std::chrono::steady_clock::time_point t) {
    start_ = t;
    return *this;
  }

  PutRecordsContext& set_end(std::chrono::steady_clock::time_point t) {
    end_ = t;
    return *this;
  }

  PutRecordsContext& set_outcome(Aws::Kinesis::Model::PutRecordsOutcome o) {
    outcome_ = std::move(o);
    return *this;
  }

 private:
  std::string stream_;
  std::chrono::steady_clock::time_point start_;
  std::chrono::steady_clock::time_point end_;
  std::vector<std::shared_ptr<KinesisRecord>> records_;
  Aws::Kinesis::Model::PutRecordsOutcome outcome_;
};

} //namespace core
} //namespace kinesis
} //namespace aws

#endif //AWS_KINESIS_CORE_PUT_RECORDS_CONTEXT_H_
