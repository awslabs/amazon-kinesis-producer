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

#ifndef AMAZON_KINESIS_PRODUCER_PROCESSING_STATISTICS_LOGGER_H
#define AMAZON_KINESIS_PRODUCER_PROCESSING_STATISTICS_LOGGER_H

#include <string>
#include <ostream>
#include <atomic>
#include <thread>

#include <aws/kinesis/core/put_records_context.h>

namespace aws {
  namespace utils {

    /**
     * \brief Provides tracking of the reason a single occurred.
     *
     * This provides a simple way for the reducer to indicate the reason, or reasons, that a flush was triggered.
     * The result of this class is consumed by flush_statistics_aggregator::merge to create the couting statistics
     */
    class flush_statistics_context {
    private:
      bool is_manual_ = false; /** Set when the flush was manually triggered */
      bool is_for_record_count_ = false; /** Set when the flush was triggered by the number of records in the container */
      bool is_for_data_size_ = false; /** Set when the flush was triggered by the number of bytes in the container */
      bool is_for_predicate_match_ = false; /** Set when the predicate was matched */
      bool is_timed_ = false; /** Set when the flush is triggered by elapsed timer */

    public:
      bool manual() { return is_manual_; }
      flush_statistics_context& manual(bool value);

      bool record_count() { return is_for_record_count_; }
      flush_statistics_context& record_count(bool value);

      bool data_size() { return is_for_data_size_; }
      flush_statistics_context& data_size(bool value);

      bool predicate_match() { return is_for_predicate_match_; }
      flush_statistics_context& predicate_match(bool value);

      bool timed() { return is_timed_; }
      flush_statistics_context& timed(bool value);

      bool flush_required() {
        return is_manual_ || is_for_record_count_ || is_for_data_size_ || is_for_predicate_match_ || is_timed_;
      }

    };

    /**
     * \brief Stores the counts for each flush reason.
     *
     * This stores the counts provided by individual flush activities, and handles reporting on the flush processes.
     */
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

    /**
     * \brief Provides the display string for a flush_statistics_aggregator
     *
     * This provides a user readable display string the flush_statistics_aggregator.  This is used by the
     * processing_statistics_logger to display a flush_statistics_aggregator to the logging system.
     *
     * @param os
     * @param fs
     * @return os
     */
    std::ostream &operator<<(std::ostream &os, const flush_statistics_aggregator &fs);

    /**
     * \brief Provides logging of flush statistics and request latency.
     *
     * This provides logging of for flush_statistics_aggregator, and for request latency.  This tracks how long it
     * takes from a PutRecordsRequest to be enqueued for processing, until it has been completed.
     */
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
