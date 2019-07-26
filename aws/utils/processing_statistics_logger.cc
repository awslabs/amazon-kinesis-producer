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

#include "processing_statistics_logger.h"
#include <aws/utils/logging.h>
#include <utility>
#include <chrono>

using namespace aws::utils;

flush_statistics_context& flush_statistics_context::manual(bool value) {
  is_manual_ = value;
  return *this;
}

flush_statistics_context& flush_statistics_context::record_count(bool value) {
  is_for_record_count_ = value;
  return *this;
}

flush_statistics_context& flush_statistics_context::data_size(bool value) {
  is_for_data_size_ = value;
  return *this;
}

flush_statistics_context& flush_statistics_context::predicate_match(bool value) {
  is_for_predicate_match_ = value;
  return *this;
}

flush_statistics_context& flush_statistics_context::timed(bool value) {
  is_timed_ = value;
  return *this;
}

flush_statistics_aggregator::flush_statistics_aggregator(const std::string &stream_name, const std::string &input_type,
                                                         const std::string &output_type) :
        stream_name_(stream_name),
        input_type_(input_type),
        output_type_(output_type) {
  reset();
}

void flush_statistics_aggregator::reset() {
  manual_ = 0;
  record_count_ = 0;
  data_size_ = 0;
  predicate_match_ = 0;
  timed_ = 0;
  input_records_ = 0;
  output_records_ = 0;
}

void flush_statistics_aggregator::merge(flush_statistics_context &flush_reason, std::uint64_t input_records) {
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

std::ostream &aws::utils::operator<<(std::ostream &os, const flush_statistics_aggregator &fs) {
  return os << "{ stream: '" << fs.stream_name_ << "', manual: " << fs.manual_ << ", count: " << fs.record_count_
            << ", size: " << fs.data_size_ << ", matches: " << fs.predicate_match_ << ", timed: " << fs.timed_
            << ", " << fs.input_type_ << ": " << fs.input_records_ << ", "
            << fs.output_type_ << ": " << fs.output_records_ << " }";
}

processing_statistics_logger::processing_statistics_logger(std::string &stream, const std::uint64_t max_buffer_time) :
        stream_(stream),
        stage1_(stream, "UserRecords", "KinesisRecords"),
        stage2_(stream, "KinesisRecords", "PutRecords"),
        total_time_(0),
        total_requests_(0),
        max_buffer_time_(max_buffer_time),
        is_running_(true),
        reporting_thread_(std::bind(&processing_statistics_logger::reporting_loop, this)) {}

void processing_statistics_logger::reporting_loop() {
  while(is_running_.load()) {
    {
      using namespace std::chrono_literals;
      std::this_thread::sleep_for(15s);
    }
    LOG(info) << "Stage 1 Triggers: " << stage1_;
    stage1_.reset();

    LOG(info) << "Stage 2 Triggers: " << stage2_;
    stage2_.reset();

    std::uint64_t total_time = total_time_;
    std::uint64_t requests = total_requests_;

    total_time_ = 0;
    total_requests_ = 0;

    double average_req_time = total_time / static_cast<double>(requests);
    double max_buffer_warn_limit = max_buffer_time_ * 5.0;
    if (average_req_time > max_buffer_warn_limit) {
      LOG(warning) << "PutRecords processing time is taking longer than " << max_buffer_warn_limit << " ms to complete.  "
                   << "You may need to adjust your configuration to reduce the processing time.";
    }
    LOG(info) << "(" << stream_ << ") Average Processing Time: " << std::setprecision(8) << average_req_time << " ms";

  }
}

void processing_statistics_logger::request_complete(std::shared_ptr<aws::kinesis::core::PutRecordsContext> context) {
  total_time_.fetch_add(context->duration_millis());
  total_requests_.fetch_add(1);
}