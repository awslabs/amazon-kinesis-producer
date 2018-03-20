// Copyright 2018 Amazon.com, Inc. or its affiliates. All Rights Reserved.
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
    
};

BOOST_AUTO_TEST_SUITE(MutableStaticCredsProviderTest)

BOOST_AUTO_TEST_CASE(SinglePublisherMultipleReaders) {
  const std::uint32_t kReaderThreadCount = 20;
  std::atomic<bool> test_running(true);
  std::array<ResultsCounter, kReaderThreadCount> results;
  std::array<aws::auth::DebugStats, kReaderThreadCount> debug_stats;
  std::vector<std::thread> reader_threads;

  aws::auth::MutableStaticCredentialsProvider provider("initial-0", "initial-0", "initial-0");
    

  std::thread producer_thread([&] {
      using namespace std::chrono;
      auto start = system_clock::now();
      auto end = start + 10s;
      system_clock::time_point now;
      std::uint32_t counter = 0;
      do {
        now = system_clock::now();
        ++counter;
        using namespace std::chrono_literals;
//        std::this_thread::sleep_for(50ms);
        std::stringstream ss;
        std::time_t now_time = system_clock::to_time_t(now);
        ss << std::put_time(std::gmtime(&now_time), "%FT%T") << "-" << std::setfill('0') << std::setw(10) << counter;
        std::string value = ss.str();
        // std::cout << "Setting Creds to " << value << std::endl;
        provider.set_credentials(value, value, value);
      } while (now < end);
    });

  for(std::uint32_t i = 0; i < kReaderThreadCount; ++i) {
    reader_threads.emplace_back([i, &provider, &results, &test_running, &debug_stats] {
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
        debug_stats[i] = provider.get_debug_stats();
      });
  }

  producer_thread.join();
  test_running = false;

  std::for_each(reader_threads.begin(), reader_threads.end(), [](std::thread& t) { t.join(); });
  std::cout << "Results" << std::endl;
  std::uint32_t results_width = 20;
  std::cout << "\t"
            << std::setw(results_width) << "Mabtched"
            << std::setw(results_width) << "Mismatched"
            << std::setw(results_width) << "Total"
            << std::setw(results_width) << "Calls/s"
            << std::endl;
  std::for_each(results.begin(), results.end(), [results_width](ResultsCounter& c) {
      double calls_per = c.total_ / c.seconds_;
      std::cout << "\t"
                << std::setw(results_width) << c.matched_
                << std::setw(results_width) << c.mismatched_
                << std::setw(results_width) << c.total_
                << std::setw(results_width) << std::setprecision(10) << calls_per
                << std::endl;
    });

  std::uint32_t debug_width = 20;
  std::cout << "Debug Stats" << std::endl;
  std::cout << "\t"
            << std::setw(debug_width) << "Update Before Load"
            << std::setw(debug_width) << "Update After Load"
            << std::setw(debug_width) << "Version Mismatch"
            << std::setw(debug_width) << "Retried"
            << std::setw(debug_width) << "Success"
            << std::setw(debug_width) << "Total"
            << std::endl;
  std::for_each(debug_stats.begin(), debug_stats.end(), [debug_width](aws::auth::DebugStats& d) {
      std::cout << "\t"
                << std::setw(debug_width) << d.update_before_load_
                << std::setw(debug_width) << d.update_after_load_
                << std::setw(debug_width) << d.version_mismatch_
                << std::setw(debug_width) << d.retried_
                << std::setw(debug_width) << d.success_
                << std::setw(debug_width) << d.attempts_
                << std::endl;
    });
    
    
}

BOOST_AUTO_TEST_SUITE_END()
