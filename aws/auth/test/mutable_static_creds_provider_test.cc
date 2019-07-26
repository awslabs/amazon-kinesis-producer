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

#include "../mutable_static_creds_provider.h"

#include <chrono>
#include <thread>
#include <array>
#include <vector>
#include <iomanip>
#include <cstdint>
#include <ctime>
#include <iostream>

#include <boost/test/unit_test.hpp>

#include <aws/utils/logging.h>

struct ResultsCounter {
  std::uint64_t matched_;
  std::uint64_t mismatched_;
  std::uint64_t total_;
  double seconds_;

  ResultsCounter() :
    matched_(0), mismatched_(0), total_(0) {}

  void match() {
    ++matched_;
    ++total_;
  }

  void mismatch() {
    ++mismatched_;
    ++total_;
  }

  ResultsCounter& operator+=(const ResultsCounter& other) {
    matched_ += other.matched_;
    mismatched_ += other.mismatched_;
    total_ += other.total_;
    seconds_ += other.seconds_;

    return *this;
  }
};

BOOST_AUTO_TEST_SUITE(MutableStaticCredsProviderTest)

BOOST_AUTO_TEST_CASE(SinglePublisherMultipleReaders) {
  const std::uint32_t kReaderThreadCount = 128;
  std::atomic<bool> test_running(true);
  std::array<ResultsCounter, kReaderThreadCount> results;
  std::vector<std::thread> reader_threads;

  aws::auth::MutableStaticCredentialsProvider provider("initial-0", "initial-0", "initial-0");

  LOG(info) << "Starting producer thread";

  std::thread producer_thread([&] {
      using namespace std::chrono;
      auto start = system_clock::now();
      auto end = start + 10s;
      system_clock::time_point now;
      std::uint32_t counter = 0;
      do {
        now = system_clock::now();
        ++counter;
        std::stringstream ss;
        std::time_t now_time = system_clock::to_time_t(now);
        ss << now_time << "-" << std::setfill('0') << std::setw(10) << counter;
        std::string value = ss.str();
        provider.set_credentials(value, value, value);
        using namespace std::chrono_literals;
        std::this_thread::sleep_for(20ms);
      } while (now < end);
      LOG(info) << "Producer thread completed";
    });

  LOG(info) << "Starting " << kReaderThreadCount << " consumer threads";
  for(std::uint32_t i = 0; i < kReaderThreadCount; ++i) {
    reader_threads.emplace_back([i, &provider, &results, &test_running] {
        using namespace std::chrono;
        auto start = high_resolution_clock::now();
        while(test_running) {
          Aws::Auth::AWSCredentials creds = provider.GetAWSCredentials();
          if (creds.GetAWSAccessKeyId() != creds.GetAWSSecretKey() && creds.GetAWSSecretKey() != creds.GetSessionToken()) {
            results[i].mismatch();
          } else {
            results[i].match();
          }
        }
        auto end = high_resolution_clock::now();
        auto taken = end - start;
        milliseconds millis_taken = duration_cast<milliseconds>(taken);
        double seconds = millis_taken.count() / 1000.0;
        results[i].seconds_ = seconds;
      });
  }

  LOG(info) << "Waiting for producer thread to complete";
  producer_thread.join();

  LOG(info) << "Notifying consumers to exit";
  test_running = false;
  std::for_each(reader_threads.begin(), reader_threads.end(), [](std::thread& t) { t.join(); });
  LOG(info) << "All consumers completed";
  
  LOG(info) << "Results";
  std::uint32_t results_width = 20;
  LOG(info) << "\t"
            << std::setw(results_width) << "Matched"
            << std::setw(results_width) << "Mismatched"
            << std::setw(results_width) << "Total"
            << std::setw(results_width) << "Calls/s";

  ResultsCounter overall;
  std::uint64_t failures = 0;

  std::for_each(results.begin(), results.end(), [results_width, &overall](ResultsCounter& c) {
      overall += c;
      double calls_per = c.total_ / c.seconds_;
      LOG(info) << "\t"
                << std::setw(results_width) << c.matched_
                << std::setw(results_width) << c.mismatched_
                << std::setw(results_width) << c.total_
                << std::setw(results_width) << std::setprecision(10) << calls_per;
    });

  double divisor = results.size() * 1.0;
  double total_average = overall.total_ / divisor;
  double matched_average = overall.matched_ / divisor;
  double mismatched_average = overall.mismatched_ / divisor;
  double average_time = overall.seconds_ / divisor;
  double overall_calls_per = total_average / average_time;

  LOG(info) << "Averages";
  LOG(info) << "\t"
            << std::setw(results_width) << matched_average
            << std::setw(results_width) << mismatched_average
            << std::setw(results_width) << total_average
            << std::setw(results_width) << overall_calls_per;
  
  BOOST_CHECK_EQUAL(failures, 0);
  LOG(info) << "Test Completed";
}

BOOST_AUTO_TEST_SUITE_END()
