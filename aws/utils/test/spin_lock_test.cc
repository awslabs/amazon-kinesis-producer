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
#ifdef DEBUG
  std::vector<typename Mutex::DebugStats> debug_stats;
  debug_stats.resize(num_threads);
#endif

  std::size_t thread_count = 0;
  while (threads.size() < num_threads) {
    std::size_t thread_index = thread_count++;
    threads.push_back(aws::thread([&, thread_index] {
      aws::utils::sleep_until(start);
      for (size_t i = 0; i < cycles_per_thread; i++) {
        aws::lock_guard<Mutex> lk(mutex);
        for (size_t j = 0; j < loop_per_cycle; j++) {
          counter++;
        }
      }
#ifdef DEBUG
      debug_stats[thread_index] = mutex.get_debug_stats();
#endif
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

#ifdef DEBUG
  LOG(info) << test_name << ": DebugStats";
  LOG(info) << "\t"
            << std::setw(20) << "Acquired Count"
            << std::setw(20) << "Acquired with Lock"
            << std::setw(20) << "Total Spins";
  std::for_each(debug_stats.begin(), debug_stats.end(), [](typename Mutex::DebugStats& d) {
      LOG(info) << "\t"
                << std::setw(20) << d.acquired_count
                << std::setw(20) << d.acquired_with_lock
                << std::setw(20) << d.total_spins;
    });
#endif
}

} //namespace

BOOST_AUTO_TEST_SUITE(SpinLock)

BOOST_AUTO_TEST_CASE(SpinLock) {
  for (size_t i : {1, 4, 8}) {
    test<aws::utils::SpinLock, 100000>("SpinLock", i);
  }
}

BOOST_AUTO_TEST_CASE(TicketSpinLock) {
  for (size_t i : {1, 4, 8, 16, 32}) {
    test<aws::utils::TicketSpinLock, 100000>("TicketSpinLock", i);
  }
}

BOOST_AUTO_TEST_CASE(StdMutex) {
  for (size_t i : {1, 4, 8}) {
    test<aws::mutex, 100000>("std::mutex", i);
  }
}

BOOST_AUTO_TEST_SUITE_END()
