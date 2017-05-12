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

#ifndef AMAZON_KINESIS_PRODUCER_PROCESSING_STATISTICS_LOGGER_H
#define AMAZON_KINESIS_PRODUCER_PROCESSING_STATISTICS_LOGGER_H

#include <string>
#include <ostream>
#include <atomic>
#include <thread>

#include <aws/kinesis/core/put_records_context.h>

namespace aws {
  namespace utils {

    class flush_statistics_context {
    private:
      bool manual_ = false;
      bool record_count_ = false;
      bool data_size_ = false;
      bool predicate_match_ = false;
      bool timed_ = false;

    public:
      bool &manual() { return manual_; }

      bool &record_count() { return record_count_; }

      bool &data_size() { return data_size_; }

      bool &predicate_match() { return predicate_match_; }

      bool &timed() { return timed_; }

      bool flush_required() {
        return manual_ || record_count_ || data_size_ || predicate_match_ || timed_;
      }

    };

    class flush_statistics_aggregator {
    public:

      flush_statistics_aggregator(const std::string &stream_name, const std::string &input_type, const std::string &output_type);

      void merge(flush_statistics_context &flush_reason, std::uint64_t input_records);

      void reset();

      flush_statistics_aggregator(const flush_statistics_aggregator &) = delete;

      flush_statistics_aggregator(flush_statistics_aggregator &&) = delete;

    private:
      std::string stream_name_;
      std::string input_type_;
      std::string output_type_;
      std::atomic<std::uint64_t> manual_;
      std::atomic<std::uint64_t> record_count_;
      std::atomic<std::uint64_t> data_size_;
      std::atomic<std::uint64_t> predicate_match_;
      std::atomic<std::uint64_t> timed_;

      std::atomic<std::uint64_t> input_records_;
      std::atomic<std::uint64_t> output_records_;

      friend std::ostream &operator<<(std::ostream &os, const flush_statistics_aggregator &fs);

    };

    class processing_statistics_logger {
    public:

      flush_statistics_aggregator &stage1() { return stage1_; }
      flush_statistics_aggregator &stage2() { return stage2_; }

      processing_statistics_logger(std::string& stream, const std::uint64_t max_buffer_time);

      void request_complete(std::shared_ptr<aws::kinesis::core::PutRecordsContext> context);

    private:

      const std::string stream_;

      flush_statistics_aggregator stage1_;
      flush_statistics_aggregator stage2_;

      const std::uint64_t max_buffer_time_;
      std::atomic<std::uint64_t> total_time_;
      std::atomic<std::uint64_t> total_requests_;

      std::thread reporting_thread_;
      std::atomic<bool> is_running_;

      void reporting_loop();
    };
  }
}



#endif //AMAZON_KINESIS_PRODUCER_PROCESSING_STATISTICS_LOGGER_H
