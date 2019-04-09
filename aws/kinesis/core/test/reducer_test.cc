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

#include <boost/test/unit_test.hpp>

#include <aws/kinesis/core/reducer.h>
#include <aws/utils/utils.h>
#include <aws/utils/io_service_executor.h>
#include <aws/kinesis/core/test/test_utils.h>
#include <aws/utils/processing_statistics_logger.h>

namespace {

using Reducer =
    aws::kinesis::core::Reducer<aws::kinesis::core::UserRecord,
                                 aws::kinesis::core::KinesisRecord>;

using FlushCallback =
    std::function<void (std::shared_ptr<aws::kinesis::core::KinesisRecord>)>;

  aws::utils::flush_statistics_aggregator flush_stats("Test", "TestRecords", "TestRecords2");

std::shared_ptr<Reducer> make_reducer(size_t size_limit = 256 * 1024,
                                      size_t count_limit = 1000,
                                      FlushCallback cb = [](auto) {}) {
  auto executor = std::make_shared<aws::utils::IoServiceExecutor>(8);
  return std::make_shared<Reducer>(executor, cb, size_limit, count_limit, flush_stats);
}

template <typename T>
class ConcurrentVector {
 public:
  ConcurrentVector(size_t capacity) {
    v_.reserve(capacity);
  }

  void push_back(const T& val) {
    aws::lock_guard<aws::mutex> lk(mutex_);
    v_.push_back(val);
  }

  T& operator [](size_t i) {
    return v_[i];
  }

  size_t size() const noexcept {
    return v_.size();
  }

 private:
  aws::mutex mutex_;
  std::vector<T> v_;
};

} //namespace

BOOST_AUTO_TEST_SUITE(Reducer)

BOOST_AUTO_TEST_CASE(CountLimit) {
  size_t limit = 100;
  auto reducer = make_reducer(0xFFFFFFFF, limit);
  std::vector<std::shared_ptr<aws::kinesis::core::UserRecord>> v;
  for (int j = 0; j < 3; j++) {
    v.clear();
    for (size_t i = 0; i < limit; i++) {
      auto ur = aws::kinesis::test::make_user_record();
      v.push_back(ur);
      auto result = reducer->add(ur);
      if (i < limit - 1) {
        BOOST_CHECK_MESSAGE(
            !result,
            "Result should be null since we havent't hit the limit");
      } else {
        BOOST_CHECK_MESSAGE(
            result,
            "Result should be non-null since we've hit the limit");
        aws::kinesis::test::verify(v, *result);
        BOOST_CHECK_EQUAL(reducer->size(), 0);
      }
    }
  }
}

BOOST_AUTO_TEST_CASE(SizeLimit) {
  size_t limit = 10000;
  auto reducer = make_reducer(limit, 0xFFFFFFFF);
  std::vector<std::shared_ptr<aws::kinesis::core::UserRecord>> v;

  int k = 0;
  for (int j = 0; j < 250; j++) {
    // Put records until flush happens.
    std::shared_ptr<aws::kinesis::core::KinesisRecord> kr;
    do {
      auto ur =
          aws::kinesis::test::make_user_record(
              "pk",
              std::to_string(::rand()),
              "123",
              10000 + k++);
      v.push_back(ur);
      kr = reducer->add(ur);
    } while (!kr);

    // Check that the count of flushed and remaining records add up to what was
    // put.
    BOOST_CHECK_EQUAL(kr->size() + reducer->size(), v.size());

    // Move the copies of those records that weren't flushed into another vector
    // so we can compare the flushed result against the original vector.
    std::vector<std::shared_ptr<aws::kinesis::core::UserRecord>> tmp(
        v.begin() + kr->size(),
        v.end());
    v.erase(v.begin() + kr->size(), v.end());

    // Check data integrity
    aws::kinesis::test::verify(v, *kr);

    // Also check we didn't exceed the limit
    auto serialized_len = kr->serialize().length();
    BOOST_CHECK_MESSAGE(serialized_len <= limit,
                        "Serialized size should be below the limit.");

    // Move the unflushed records back so they're accounted for in the next
    // iteration.
    v.clear();
    v.insert(v.end(), tmp.begin(), tmp.end());
  }
}

BOOST_AUTO_TEST_CASE(Deadline) {
  std::shared_ptr<aws::kinesis::core::KinesisRecord> kr;
  auto reducer = make_reducer(
      0xFFFFFFFF,
      0xFFFFFFFF,
      [&](auto result) {
        kr = result;
      });

  std::vector<std::shared_ptr<aws::kinesis::core::UserRecord>> v;
  for (int i = 0; i < 100; i++) {
    auto ur =
        aws::kinesis::test::make_user_record(
            "pk",
            std::to_string(::rand()),
            "123",
            5000 + i);
    v.push_back(ur);
    reducer->add(ur);
  }

  // Should not flush after 1 second because we've set the deadlines to be 5
  aws::utils::sleep_for(std::chrono::milliseconds(1000));
  BOOST_CHECK_EQUAL(reducer->size(), 100);

  // Now we're going to put a record with a deadline that's now to trigger the
  // flush.
  auto ur =
      aws::kinesis::test::make_user_record(
          "pk",
          std::to_string(::rand()),
          "123",
          0);
  reducer->add(ur);
  // This record should be moved to the front when flushed because of its
  // deadline
  v.insert(v.begin(), ur);

  aws::utils::sleep_for(std::chrono::milliseconds(300));
  BOOST_CHECK_EQUAL(reducer->size(), 0);
  BOOST_REQUIRE_MESSAGE(kr, "Async flush should have produced results");
  aws::kinesis::test::verify(v, *kr);
}

// Test that a flush due to limits cancels a deadline trigger. Flushes always
// remove records with closer deadlines first. Once they are removed, the
// Reducer's timeout should be reset to the min deadline of the records that
// remain.
BOOST_AUTO_TEST_CASE(ResetTimeout) {
  std::shared_ptr<aws::kinesis::core::KinesisRecord> kr;
  auto reducer = make_reducer(
      300,
      0xFFFFFFFF,
      [&](auto result) {
        kr = result;
      });

  std::vector<std::shared_ptr<aws::kinesis::core::UserRecord>> v;
  int k = 0;

  for (int i = 0; i < 10; i++) {
    auto ur =
        aws::kinesis::test::make_user_record(
            "pk",
            std::to_string(::rand()),
            "123",
            2000 + k++);
    v.push_back(ur);
    reducer->add(ur);
  }

  // This record has a much closer deadline
  {
    auto ur =
        aws::kinesis::test::make_user_record(
            "pk",
            std::to_string(::rand()),
            "123",
            100);
    v.insert(v.begin(), ur);
    reducer->add(ur);
  }

  // Put records until flush happens
  std::shared_ptr<aws::kinesis::core::KinesisRecord> kr2;
  {
    do {
      // The other records have a further deadline.
      auto ur =
          aws::kinesis::test::make_user_record(
              "pk",
              std::to_string(::rand()),
              "123",
              2000 + k++);
      v.push_back(ur);
      kr2 = reducer->add(ur);
    } while (!kr2);
  }

  // Put a few more to make sure the buffer is not empty
  for (int i = 0; i < 10; i++) {
    auto ur =
        aws::kinesis::test::make_user_record(
            "pk",
            std::to_string(::rand()),
            "123",
            2000 + k++);
    v.push_back(ur);
    reducer->add(ur);
  }

  // No flush should happen in the next second even though we had a record with
  // a 100ms deadline - that record should've gotten flushed by the size limit,
  // and its deadline should no longer apply.
  aws::utils::sleep_for(std::chrono::milliseconds(1000));
  BOOST_REQUIRE_MESSAGE(!kr, "No flush due to deadline should have occured");

  // The timer should now be set to the min deadline of the remaining records.
  // It should go off after another second or so
  aws::utils::sleep_for(std::chrono::milliseconds(1500));
  BOOST_REQUIRE_MESSAGE(kr, "Flush due to deadline should have occured");

  // Check data integrity
  BOOST_REQUIRE_EQUAL(kr->size() + kr2->size() + reducer->size(), v.size());
  v.erase(v.begin() + kr->size() + kr2->size(), v.end());
  std::vector<std::shared_ptr<aws::kinesis::core::UserRecord>> v2(
      v.begin(),
      v.begin() + kr2->size());
  v.erase(v.begin(), v.begin() + kr2->size());
  aws::kinesis::test::verify(v, *kr);
  aws::kinesis::test::verify(v2, *kr2);
}

// A KinesisRecord should be produced even if a UserRecord is over the size
// limit all by itself
BOOST_AUTO_TEST_CASE(GiantRecord) {
  auto reducer = make_reducer(10000, 0xFFFFFFFF);
  auto ur = aws::kinesis::test::make_user_record("pk", std::string(11000, 'a'));
  auto kr = reducer->add(ur);
  BOOST_CHECK_MESSAGE(kr, "Should've produced a KinesisRecord");
  aws::kinesis::test::verify_unaggregated(ur, *kr);
}

// Manual flush should not produce empty KinesisRecords
BOOST_AUTO_TEST_CASE(NonEmpty) {
  std::shared_ptr<aws::kinesis::core::KinesisRecord> kr;
  auto reducer = make_reducer(
      0xFFFFFFFF,
      0xFFFFFFFF,
      [&](auto result) {
        kr = result;
      });
  reducer->flush();
  aws::utils::sleep_for(std::chrono::milliseconds(100));
  BOOST_CHECK_MESSAGE(
      !kr,
      "Flush should not have produced a KinesisRecord because the Reducer is "
      " empty");
}

BOOST_AUTO_TEST_CASE(Concurrency) {
  LOG(info) << "Starting concurrency test. If this doesn't finish in 30 "
            << "seconds or so it probably means there's a deadlock.";

  ConcurrentVector<std::shared_ptr<aws::kinesis::core::UserRecord>>
      put(8 * 1024 * 1024);

  ConcurrentVector<std::shared_ptr<aws::kinesis::core::KinesisRecord>>
      results(2 * 1024 * 1024);

  std::atomic<uint64_t> counter(0);

  auto reducer = make_reducer(
      50000,
      0xFFFFFFFF,
      [&](auto result) {
        results.push_back(result);
      });

  std::vector<aws::thread> threads;
  for (int i = 0; i < 16; i++) {
    threads.push_back(aws::thread([&, i] {
      for (int j = 0; j < 32; j++) {
        std::shared_ptr<aws::kinesis::core::KinesisRecord> kr;
        do {
          auto ur = aws::kinesis::test::make_user_record("pk",
                                     std::to_string(counter++),
                                     "123",
                                     counter);
          put.push_back(ur);
          kr = reducer->add(ur);
        } while (!kr);

        results.push_back(kr);

        // Call flush sometimes too to mix things up further
        if (i % 8 == 0) {
          if (i == j) {
            reducer->flush();
          }
        }
      }
    }));
  }

  for (auto& t : threads) {
    t.join();
  }

  while (reducer->size() > 0) {
    reducer->flush();
    aws::utils::sleep_for(std::chrono::milliseconds(1000));
  }

  BOOST_CHECK_EQUAL(reducer->size(), 0);

  LOG(info) << "Finished putting data, " << counter << " records put. "
            << "Analyzing results...";

  // Check that all records made it out. Order is no longer guaranteed with
  // many workers, but we've put a monotonically increasing number in the
  // record data, so that will allow us to sort and match records up.
  std::vector<std::shared_ptr<aws::kinesis::core::UserRecord>> put_v;
  for (size_t i = 0; i < put.size(); i++) {
    put_v.push_back(put[i]);
  }

  std::vector<std::shared_ptr<aws::kinesis::core::UserRecord>> result_v;
  for (size_t i = 0; i < results.size(); i++) {
    result_v.insert(result_v.end(),
                    results[i]->items().cbegin(),
                    results[i]->items().cend());
  }

  auto cmp = [](auto& a, auto& b) {
    uint64_t id_a = std::stoull(a->data());
    uint64_t id_b = std::stoull(b->data());
    return id_a < id_b;
  };

  std::sort(put_v.begin(), put_v.end(), cmp);
  std::sort(result_v.begin(), result_v.end(), cmp);

  BOOST_REQUIRE_EQUAL(put_v.size(), result_v.size());
  for (size_t i = 0; i < put_v.size(); i++) {
    BOOST_CHECK_EQUAL(put_v[i].get(), result_v[i].get());
  }
}

BOOST_AUTO_TEST_SUITE_END()
