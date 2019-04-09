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

#include <atomic>
#include <thread>
#include <vector>

#include <boost/test/unit_test.hpp>

#include <aws/metrics/accumulator.h>
#include <aws/utils/utils.h>
#include <aws/utils/logging.h>
#include <aws/mutex.h>

namespace {

auto get_start_time() {
  return aws::utils::round_down_time<
      std::chrono::seconds,
      std::chrono::high_resolution_clock>() +
          std::chrono::seconds(2);
}

} //namespace

BOOST_AUTO_TEST_SUITE(Accumulator)

BOOST_AUTO_TEST_CASE(Overall) {
  aws::metrics::detail::AccumulatorImpl<
      double,
      std::chrono::milliseconds,
      30> a;

  int N = 100;
  for (int i = 0; i <= N; i += 1) {
    a(i);
  }

  BOOST_CHECK_EQUAL(a.count(), N + 1);
  BOOST_CHECK_EQUAL(a.min(), 0);
  BOOST_CHECK_EQUAL(a.max(), N);
  BOOST_CHECK_EQUAL(a.mean(), 50);
  BOOST_CHECK_EQUAL(a.sum(), 5050);
}

BOOST_AUTO_TEST_CASE(Window) {
  const int num_buckets = 100;
  const int num_samples = 100;
  const int last_val = num_buckets * num_samples - 1;

  using Bucket = std::chrono::duration<uint64_t, std::ratio<1, 8>>;
  aws::metrics::detail::AccumulatorImpl<double, Bucket, num_buckets> a;

  auto start = get_start_time();

  size_t v = 0;
  for (int i = 0; i < num_buckets; i += 1) {
    while (std::chrono::high_resolution_clock::now() < start + Bucket(i));
    for (int j = 0; j < num_samples; j++) {
      a(v++);
    }
  }

  BOOST_CHECK_EQUAL(a.count(0), 0);
  BOOST_CHECK_EQUAL(a.count(1), 1 * num_samples);
  BOOST_CHECK_EQUAL(a.count(15), 15 * num_samples);
  BOOST_CHECK_EQUAL(a.count(30), 30 * num_samples);

  BOOST_CHECK_EQUAL(a.min(0), 0);
  BOOST_CHECK_EQUAL(a.min(1), last_val - num_samples + 1);
  BOOST_CHECK_EQUAL(a.min(15), last_val - num_samples * 15 + 1);
  BOOST_CHECK_EQUAL(a.min(30), last_val - num_samples * 30 + 1);

  BOOST_CHECK_EQUAL(a.max(0), 0);
  BOOST_CHECK_EQUAL(a.max(1), last_val);
  BOOST_CHECK_EQUAL(a.max(15), last_val);
  BOOST_CHECK_EQUAL(a.max(30), last_val);

  BOOST_CHECK_EQUAL(a.mean(0), 0);
  BOOST_CHECK_EQUAL(a.mean(1), 9949.5);
  BOOST_CHECK_EQUAL(a.mean(15), 9249.5);
  BOOST_CHECK_EQUAL(a.mean(30), 8499.5);

  BOOST_CHECK_EQUAL(a.sum(0), 0);
  BOOST_CHECK_EQUAL(a.sum(1), 994950);
  BOOST_CHECK_EQUAL(a.sum(15), 13874250);
  BOOST_CHECK_EQUAL(a.sum(30), 25498500);

  BOOST_CHECK_EQUAL(a.count(), last_val + 1);
  BOOST_CHECK_EQUAL(a.min(), 0);
  BOOST_CHECK_EQUAL(a.max(), last_val);
  BOOST_CHECK_EQUAL(a.mean(), 4999.5);
  BOOST_CHECK_EQUAL(a.sum(), 49995000);
}

BOOST_AUTO_TEST_CASE(Concurrency) {
  const int num_threads = 10;
  const int num_buckets = 100;
  const int num_samples = 100;
  const int last_val = num_buckets * num_samples - 1;

  using Bucket = std::chrono::duration<uint64_t, std::ratio<1, 8>>;
  aws::metrics::detail::AccumulatorImpl<double, Bucket, num_buckets> a;

  auto start = get_start_time();

  std::atomic<size_t> v(0);
  std::vector<aws::thread> threads;
  for (int i = 0; i < num_threads; i++) {
    threads.emplace_back([&] {
      for (int i = 0; i < num_buckets; i += 1) {
        aws::utils::sleep_until(start + Bucket(i));
        for (int j = 0; j < num_samples / num_threads; j++) {
          a(v++);
        }
      }
    });
  }

  for (auto& t : threads) {
    t.join();
  }

  BOOST_CHECK_EQUAL(a.count(0), 0);
  BOOST_CHECK_EQUAL(a.count(1), 1 * num_samples);
  BOOST_CHECK_EQUAL(a.count(15), 15 * num_samples);
  BOOST_CHECK_EQUAL(a.count(30), 30 * num_samples);

  BOOST_CHECK_EQUAL(a.min(0), 0);
  BOOST_CHECK_EQUAL(a.min(1), last_val - num_samples + 1);
  BOOST_CHECK_EQUAL(a.min(15), last_val - num_samples * 15 + 1);
  BOOST_CHECK_EQUAL(a.min(30), last_val - num_samples * 30 + 1);

  BOOST_CHECK_EQUAL(a.max(0), 0);
  BOOST_CHECK_EQUAL(a.max(1), last_val);
  BOOST_CHECK_EQUAL(a.max(15), last_val);
  BOOST_CHECK_EQUAL(a.max(30), last_val);

  BOOST_CHECK_EQUAL(a.mean(0), 0);
  BOOST_CHECK_EQUAL(a.mean(1), 9949.5);
  BOOST_CHECK_EQUAL(a.mean(15), 9249.5);
  BOOST_CHECK_EQUAL(a.mean(30), 8499.5);

  BOOST_CHECK_EQUAL(a.sum(0), 0);
  BOOST_CHECK_EQUAL(a.sum(1), 994950);
  BOOST_CHECK_EQUAL(a.sum(15), 13874250);
  BOOST_CHECK_EQUAL(a.sum(30), 25498500);

  BOOST_CHECK_EQUAL(a.count(), last_val + 1);
  BOOST_CHECK_EQUAL(a.min(), 0);
  BOOST_CHECK_EQUAL(a.max(), last_val);
  BOOST_CHECK_EQUAL(a.mean(), 4999.5);
  BOOST_CHECK_EQUAL(a.sum(), 49995000);
}

BOOST_AUTO_TEST_CASE(WriteThroughput) {
  const int N = 100000;

  for (size_t num_threads = 1; num_threads <= 8; num_threads *= 2) {
    aws::metrics::Accumulator a;

    auto start = get_start_time();

    std::atomic<size_t> v(0);
    std::vector<aws::thread> threads;
    for (size_t i = 0; i < num_threads; i++) {
      threads.emplace_back([&] {
        aws::utils::sleep_until(start);
        while (true) {
          size_t z = v++;
          if (z < N) {
            a(z);
          } else {
            break;
          }
        }
      });
    }

    for (auto& t : threads) {
      t.join();
    }

    double rate = (double) N / aws::utils::seconds_since(start);
    LOG(info) << "Accumulator tight loop put rate (" << num_threads
              << " threads): " << rate / 1000 << " K per second ";
  }
}

BOOST_AUTO_TEST_SUITE_END()
