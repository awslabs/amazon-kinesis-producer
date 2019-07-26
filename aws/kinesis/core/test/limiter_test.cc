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

#include <unordered_set>

#include <boost/test/unit_test.hpp>

#include <aws/kinesis/core/limiter.h>
#include <aws/kinesis/core/test/test_utils.h>
#include <aws/utils/io_service_executor.h>
#include <aws/utils/logging.h>

namespace {

using Clock = std::chrono::steady_clock;
using TimePoint = Clock::time_point;

using Callback =
    std::function<void (
        const std::shared_ptr<aws::kinesis::core::KinesisRecord>&)>;

auto make_kinesis_record(
    TimePoint deadline = Clock::now(),
    TimePoint expiration = TimePoint::max()) {
  auto ur = aws::kinesis::test::make_user_record();
  ur->set_deadline(deadline);
  ur->set_expiration(expiration);
  ur->predicted_shard(0);
  auto kr = std::make_shared<aws::kinesis::core::KinesisRecord>();
  kr->add(ur);
  return kr;
}

auto make_limiter(Callback cb, Callback expired_cb = [](auto) {}) {
  auto executor = std::make_shared<aws::utils::IoServiceExecutor>(2);
  auto config = std::make_shared<aws::kinesis::core::Configuration>();
  config->rate_limit(100);
  return std::make_shared<aws::kinesis::core::Limiter>(
      executor,
      cb,
      expired_cb,
      config);
}

} //namespace

BOOST_AUTO_TEST_SUITE(Limiter)

// Test that the limiter emits records ordered by deadline, closest first
BOOST_AUTO_TEST_CASE(Ordering) {
  std::vector<std::shared_ptr<aws::kinesis::core::KinesisRecord>> original;
  std::vector<std::shared_ptr<aws::kinesis::core::KinesisRecord>> result;

  auto limiter = make_limiter([&](auto kr) {
    result.push_back(kr);
  });

  // We make every deadline unique since there's no guarantee on the relative
  // order of two records with the same deadline.
  auto first_deadline = Clock::now();

  // First, put a bunch of records to drain the token bucket and fill up the
  // limiter's queue
  size_t initial_batch_size = 2000;
  for (size_t i = 0; i < initial_batch_size; i++) {
    auto kr = make_kinesis_record(
        first_deadline + std::chrono::milliseconds(i));
    original.push_back(kr);
    limiter->put(kr);
  }

  // Then put records with random deadlines
  std::srand(1337);
  std::unordered_set<size_t> used;
  for (int i = 0; i < 2000; i++) {
    size_t delta;
    do {
      delta = std::rand() % 100000;
    } while (used.find(delta) != used.end());
    used.insert(delta);

    auto kr = make_kinesis_record(
        first_deadline +
            std::chrono::milliseconds(initial_batch_size + delta));
    original.push_back(kr);
    limiter->put(kr);
  }

  while (result.size() < original.size()) {
    aws::utils::sleep_for(std::chrono::milliseconds(1000));
    LOG(info) << result.size() << " / " << original.size();
  }

  // Check that the records were emitted in the order of their deadlines
  BOOST_REQUIRE_EQUAL(original.size(), result.size());

  std::sort(original.begin(), original.end(), [](auto a, auto b) {
    return a->deadline() < b->deadline();
  });

  for (size_t i = 0; i < original.size(); i++) {
    if (original[i] != result[i]) {
      std::stringstream ss;
      ss << "failed (" << i << ") "
         << "original: " << original[i]->deadline().time_since_epoch().count()
         << ", result: " << result[i]->deadline().time_since_epoch().count();
      BOOST_FAIL(ss.str());
    }
  }
}

// Test that the limiter removes records that have expired while sitting in the
// queue
BOOST_AUTO_TEST_CASE(Expiration) {
  std::vector<std::shared_ptr<aws::kinesis::core::KinesisRecord>>
      expect_sent,
      expect_expired,
      sent,
      expired;

  auto limiter = make_limiter([&](auto kr) { sent.push_back(kr); },
                              [&](auto kr) { expired.push_back(kr); });

  // First, put a bunch of records to drain the token bucket and fill up the
  // limiter's queue. These records won't ever expire.
  for (size_t i = 0; i < 3000; i++) {
    auto kr = make_kinesis_record();
    expect_sent.push_back(kr);
    limiter->put(kr);
  }

  std::srand(1337);

  // Put some records with expiration in the past
  for (size_t i = 0; i < 1000; i++) {
    auto kr =
        make_kinesis_record(
            Clock::now() + std::chrono::milliseconds(1),
            Clock::now() - std::chrono::milliseconds(std::rand() % 1000000));
    expect_expired.push_back(kr);
    limiter->put(kr);
  }

  // Put some records that are not expired, but will expire while waiting in the
  // queue
  for (size_t i = 0; i < 1000; i++) {
    auto kr =
        make_kinesis_record(
            Clock::now() + std::chrono::milliseconds(1),
            Clock::now() + std::chrono::milliseconds(1000 + std::rand() % 100));
    BOOST_REQUIRE(!kr->expired());
    expect_expired.push_back(kr);
    limiter->put(kr);
  }

  while (sent.size() < expect_sent.size() ||
         expired.size() < expect_expired.size()) {
    aws::utils::sleep_for(std::chrono::milliseconds(1000));
    LOG(info) << sent.size() << " / " << expect_sent.size() << "; "
              << expired.size() << " / " << expect_expired.size();
  }

  BOOST_REQUIRE_EQUAL(expect_sent.size(), sent.size());
  BOOST_REQUIRE_EQUAL(expect_expired.size(), expired.size());
}

BOOST_AUTO_TEST_SUITE_END()
