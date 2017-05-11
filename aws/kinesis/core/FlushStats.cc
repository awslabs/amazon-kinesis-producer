// Copyright 2017 Amazon.com, Inc. or its affiliates. All Rights Reserved.
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

#include "FlushStats.h"

using namespace aws::kinesis::core;

FlushStats::FlushStats(const std::string &stream_name, const std::string &input_type, const std::string &output_type) :
        stream_name_(stream_name),
        input_type_(input_type),
        output_type_(output_type) {
  reset();
}

void FlushStats::reset() {
  manual_ = 0;
  record_count_ = 0;
  data_size_ = 0;
  predicate_match_ = 0;
  timed_ = 0;
  input_records_ = 0;
  output_records_ = 0;
}

void FlushStats::merge(FlushReason &flush_reason, std::uint64_t input_records) {
  if (flush_reason.manual()) {
    ++manual_;
  }
  if (flush_reason.record_count()) {
    ++record_count_;
  }
  if (flush_reason.data_size()) {
    ++data_size_;
  }
  if (flush_reason.predicate_match()) {
    ++predicate_match_;
  }
  if (flush_reason.timed()) {
    ++timed_;
  }

  input_records_ += input_records;
  ++output_records_;

}

std::ostream &aws::kinesis::core::operator<<(std::ostream &os, const FlushStats &fs) {
  return os << "{ stream: '" << fs.stream_name_ << "', manual: " << fs.manual_ << ", count: " << fs.record_count_
            << ", size: " << fs.data_size_ << ", matches: " << fs.predicate_match_ << ", timed: " << fs.timed_
            << ", " << fs.input_type_ << ": " << fs.input_records_ << ", "
            << fs.output_type_ << ": " << fs.output_records_ << " }";
}