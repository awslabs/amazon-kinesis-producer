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

#include <chrono>
#include <vector>

#include <boost/test/unit_test.hpp>

#include <aws/utils/logging.h>

#include <aws/utils/concurrent_linked_queue.h>
#include <aws/mutex.h>

namespace {

std::string add_leading_zeroes(size_t total_len, size_t n) {
  auto s = std::to_string(n);
  assert(s.length() <= total_len);
  return std::string(total_len - s.length(), '0') + s;
}

std::string make_item(size_t i, size_t j) {
  return add_leading_zeroes(3, i) + "_" + add_leading_zeroes(8, j);
}

} //namespace

BOOST_AUTO_TEST_SUITE(ConcurrentLinkedQueue)

BOOST_AUTO_TEST_CASE(Sequential) {
  aws::utils::ConcurrentLinkedQueue<std::string> q;

  for (int i = 0; i < 100; i++) {
    for (int j = 0; j < i; j++) {
      q.put(std::to_string(j));
    }
    std::string s;
    for (int j = 0; j < i; j++) {
      BOOST_REQUIRE(q.try_take(s));
      BOOST_CHECK_EQUAL(s, std::to_string(j));
    }
  }
}

BOOST_AUTO_TEST_CASE(Destructor) {
  aws::utils::ConcurrentLinkedQueue<std::string> q;
  for (int j = 0; j < 100; j++) {
    q.put(std::to_string(j));
  }
  // should not crash during dealloc with items remaining in the queue
}

BOOST_AUTO_TEST_CASE(Concurrent) {
  const size_t total_num_items = 1 << 20;

  auto f = [&](const size_t num_threads) {
    LOG(info) << "Starting concurrent queue test for " << num_threads
              << " threads...";

    aws::utils::ConcurrentLinkedQueue<std::string> q;
    aws::utils::ConcurrentLinkedQueue<std::vector<std::string>> results;

    std::atomic<size_t> ready(0);
    std::vector<aws::thread> threads;
    std::atomic<size_t> dequeued(0);
    std::chrono::high_resolution_clock::time_point start;
    std::chrono::high_resolution_clock::time_point end;

    for (size_t i = 0; i < num_threads; i++) {
      threads.emplace_back([&, i] {
        const size_t num_items = total_num_items / (num_threads / 2);

        if (i % 2 == 0) {
          std::vector<std::string> items;
          items.reserve(num_items);
          for (size_t j = 0; j < num_items; j++) {
            items.push_back(make_item(i / 2, j));
          }

          ready++;
          while (ready < num_threads) {
            aws::this_thread::yield();
          }
          if (start.time_since_epoch().count() == 0) {
            start = std::chrono::high_resolution_clock::now();
          }

          for (size_t j = 0; j < num_items; j++) {
            q.put(std::move(items[j]));
          }
        } else {
          std::vector<std::string> read;
          read.reserve(2 * num_items);

          ready++;
          while (ready < num_threads) {
            aws::this_thread::yield();
          }
          if (start.time_since_epoch().count() == 0) {
            start = std::chrono::high_resolution_clock::now();
          }

          std::string s;
          while (dequeued < total_num_items) {
            if (q.try_take(s)) {
              read.push_back(std::move(s));
              dequeued++;
            }
          }

          end = std::chrono::high_resolution_clock::now();

          results.put(std::move(read));
        }
      });
    }

    for (auto& t : threads) {
      t.join();
    }

    LOG(info) << "Done. Analyzing results...";

    std::vector<std::string> total;
    total.reserve(total_num_items);
    std::vector<std::string> tmp;
    while (results.try_take(tmp)) {
      std::move(tmp.begin(), tmp.end(), std::back_inserter(total));
    }
    BOOST_REQUIRE_EQUAL(total.size(), total_num_items);

    std::sort(total.begin(), total.end());
    auto it = total.begin();
    for (size_t i = 0; i < num_threads / 2; i++) {
      for (size_t j = 0; j < total_num_items / (num_threads / 2); j++) {
        BOOST_REQUIRE_EQUAL(*it, make_item(i, j));
        it++;
      }
    }

    LOG(info) << "Passed.";
    return end - start;
  };

  const std::size_t kThreadCount = 128;

  for (size_t num_threads = 2;
       num_threads < kThreadCount;
       num_threads *= 2) {
    auto duration = f(num_threads);
    auto nanos =
        std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();
    double seconds = nanos / 1e9;
    LOG(info) << "ConcurrentLinkedQueue throughput ("
              << num_threads << " threads): "
              << total_num_items << " items, "
              << seconds << " seconds, "
              << total_num_items / seconds / 1000 << " K/s";
  }
}

BOOST_AUTO_TEST_SUITE_END()
