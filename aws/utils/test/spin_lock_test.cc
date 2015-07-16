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

#include <vector>

#include <boost/test/unit_test.hpp>

#include <aws/mutex.h>
#include <aws/utils/logging.h>
#include <aws/utils/spin_lock.h>
#include <aws/utils/utils.h>

namespace {

template <typename Mutex,
          size_t cycles_per_thread,
          size_t loop_per_cycle = 100>
void test(const std::string test_name,
          const size_t num_threads = aws::thread::hardware_concurrency()) {
  auto start = std::chrono::high_resolution_clock::now() +
      std::chrono::milliseconds(100);

  Mutex mutex;
  volatile size_t counter = 0;
  std::vector<aws::thread> threads;

  while (threads.size() < num_threads) {
    threads.push_back(aws::thread([&] {
      aws::utils::sleep_until(start);
      for (size_t i = 0; i < cycles_per_thread; i++) {
        aws::lock_guard<Mutex> lk(mutex);
        for (size_t j = 0; j < loop_per_cycle; j++) {
          counter++;
        }
      }
    }));
  }

  for (auto& t : threads) {
    t.join();
  }

  double seconds = aws::utils::seconds_since(start);

  BOOST_REQUIRE_EQUAL(counter,
                      cycles_per_thread * loop_per_cycle * num_threads);

  LOG(info) << test_name << ": " << num_threads << " threads: "
            << cycles_per_thread * num_threads / seconds
            << " ops per sec";
}

} //namespace

BOOST_AUTO_TEST_SUITE(SpinLock)

BOOST_AUTO_TEST_CASE(SpinLock) {
  for (size_t i : {1, 4, 8}) {
    test<aws::utils::SpinLock, 100000>("SpinLock", i);
  }
}

BOOST_AUTO_TEST_CASE(TicketSpinLock) {
  for (size_t i : {1, 4, 8}) {
    test<aws::utils::TicketSpinLock, 100000>("TicketSpinLock", i);
  }
}

BOOST_AUTO_TEST_CASE(StdMutex) {
  for (size_t i : {1, 4, 8}) {
    test<aws::mutex, 100000>("std::mutex", i);
  }
}

BOOST_AUTO_TEST_SUITE_END()
