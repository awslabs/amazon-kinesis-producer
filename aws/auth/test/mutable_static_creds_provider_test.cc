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
        std::this_thread::sleep_for(1s);
        std::stringstream ss;
        std::time_t now_time = system_clock::to_time_t(now);
        ss << std::put_time(std::gmtime(&now_time), "%FT%T") << "-" << std::setfill('0') << std::setw(10) << counter;
        std::string value = ss.str();
        std::cout << "Setting Creds to " << value << std::endl;
        provider.set_credentials(value, value, value);
      } while (now < end);
    });

  for(std::uint32_t i = 0; i < kReaderThreadCount; ++i) {
    reader_threads.emplace_back([i, &provider, &results, &test_running] {
        while(test_running) {
          Aws::Auth::AWSCredentials creds = provider.GetAWSCredentials();
          if (creds.GetAWSAccessKeyId() != creds.GetAWSSecretKey() && creds.GetAWSSecretKey() != creds.GetSessionToken()) {
            results[i].mismatch();
          } else {
            results[i].match();
          }
        }
      });
  }

  producer_thread.join();
  test_running = false;

  std::for_each(reader_threads.begin(), reader_threads.end(), [](std::thread& t) { t.join(); });
  std::cout << "Results" << std::endl;
  std::cout << "\t" << std::setw(15) << "Matched" << std::setw(15) << "Mismatched" << std::setw(15) << "Total" << std::endl;
  std::for_each(results.begin(), results.end(), [](ResultsCounter& c) {
      std::cout << "\t" << std::setw(15) << c.matched_ << std::setw(15) << c.mismatched_ << std::setw(15) << c.total_ << std::endl;
    });
    
}

BOOST_AUTO_TEST_SUITE_END()
