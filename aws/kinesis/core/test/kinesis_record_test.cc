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

#include <aws/kinesis/core/test/test_utils.h>

BOOST_AUTO_TEST_SUITE(KinesisRecord)

// Single records should not be turned into AggregatedRecord's
BOOST_AUTO_TEST_CASE(SingleRecord) {
  // No explicit hash key (ehk)
  {
    aws::kinesis::core::KinesisRecord r;
    auto ur = aws::kinesis::test::make_user_record();
    r.add(ur);
    aws::kinesis::test::verify_unaggregated(ur, r);
  }

  // With ehk
  {
    aws::kinesis::core::KinesisRecord r;
    auto ur = aws::kinesis::test::make_user_record("a", "a", "123");
    r.add(ur);
    aws::kinesis::test::verify_unaggregated(ur, r);
  }
}

// In the next 6 test cases we add a bunch of UserRecords to a KinesisRecord,
// then we remove some. Each case has a different distribution of partition and
// explicit hash keys. These tests exercise the add and remove methods of
// KinesisRecord. In practice we typically add records until the KinesisRecord
// has an estimated size that's over some threshold, then we remove records
// until the accurate size is at or below that threshold.

// All the records have the same partition key
BOOST_AUTO_TEST_CASE(SameParitionKey) {
  int N = 1000;
  int M = 100;

  std::vector<std::shared_ptr<aws::kinesis::core::UserRecord>> user_records;
  aws::kinesis::core::KinesisRecord r;
  for (int i = 0; i < N; i++) {
    auto ur = aws::kinesis::test::make_user_record();
    user_records.push_back(ur);
    r.add(ur);
  }
  for (int i = 0; i < M; i++) {
    r.remove_last();
    user_records.pop_back();
  }

  aws::kinesis::test::verify(user_records, r);
}

// Every record has a different key
BOOST_AUTO_TEST_CASE(DifferentParitionKey) {
  int N = 1000;
  int M = 100;

  std::vector<std::shared_ptr<aws::kinesis::core::UserRecord>> user_records;
  aws::kinesis::core::KinesisRecord r;
  for (int i = 0; i < N; i++) {
    auto ur = aws::kinesis::test::make_user_record(std::to_string(i));
    user_records.push_back(ur);
    r.add(ur);
  }
  for (int i = 0; i < M; i++) {
    r.remove_last();
    user_records.pop_back();
  }

  aws::kinesis::test::verify(user_records, r);
}

// Mix of single and duplicate keys, some contiguous, some not
BOOST_AUTO_TEST_CASE(MixedParitionKey) {
  std::vector<std::string> keys ({
    "a", "b", "b", "a", "a", "c", "b", "a", "d", "e", "e", "d", "d", "d"
  });

  std::vector<std::shared_ptr<aws::kinesis::core::UserRecord>> user_records;
  aws::kinesis::core::KinesisRecord r;
  for (size_t i = 0; i < keys.size(); i++) {
    auto ur = aws::kinesis::test::make_user_record(keys[i]);
    user_records.push_back(ur);
    r.add(ur);
  }
  for (int i = 0; i < 3; i++) {
    r.remove_last();
    user_records.pop_back();
  }

  aws::kinesis::test::verify(user_records, r);
}

BOOST_AUTO_TEST_CASE(SameEHK) {
  int N = 1000;
  int M = 100;

  std::vector<std::shared_ptr<aws::kinesis::core::UserRecord>> user_records;
  aws::kinesis::core::KinesisRecord r;
  for (int i = 0; i < N; i++) {
    auto ur = aws::kinesis::test::make_user_record("pk", "data", "123");
    user_records.push_back(ur);
    r.add(ur);
  }
  for (int i = 0; i < M; i++) {
    r.remove_last();
    user_records.pop_back();
  }

  aws::kinesis::test::verify(user_records, r);
}

BOOST_AUTO_TEST_CASE(DifferentEHK) {
  int N = 1000;
  int M = 100;

  std::vector<std::shared_ptr<aws::kinesis::core::UserRecord>> user_records;
  aws::kinesis::core::KinesisRecord r;
  for (int i = 0; i < N; i++) {
    auto ur =
        aws::kinesis::test::make_user_record("pk", "data", std::to_string(i));
    user_records.push_back(ur);
    r.add(ur);
  }
  for (int i = 0; i < M; i++) {
    r.remove_last();
    user_records.pop_back();
  }

  aws::kinesis::test::verify(user_records, r);
}

BOOST_AUTO_TEST_CASE(MixedEHK) {
  std::vector<std::string> keys ({
    "1", "2", "2", "1", "1", "3", "2", "1", "4", "5", "5", "4", "4", "4"
  });

  std::vector<std::shared_ptr<aws::kinesis::core::UserRecord>> user_records;
  aws::kinesis::core::KinesisRecord r;
  for (size_t i = 0; i < keys.size(); i++) {
    auto ur = aws::kinesis::test::make_user_record("pk", "data", keys[i]);
    user_records.push_back(ur);
    r.add(ur);
  }
  for (int i = 0; i < 3; i++) {
    r.remove_last();
    user_records.pop_back();
  }

  aws::kinesis::test::verify(user_records, r);
}

// aws::kinesis::test::verify accurate_size. Since this is probabilistic, it's not
// guaranteed to catch all errors. It should however detect any obvious
// mistakes.
BOOST_AUTO_TEST_CASE(AccurateSize) {
  for (int i = 0; i < 100; i++) {
    aws::kinesis::core::KinesisRecord r;
    uint64_t num_records = aws::utils::random_int(1, 512);
    for (uint64_t j = 0; j < num_records; j++) {
      uint64_t key_size = aws::utils::random_int(1, 256);
      uint64_t data_size = aws::utils::random_int(1, 64 * 1024);
      auto ur = aws::kinesis::test::make_user_record(std::string(key_size, 'a'),
                                 std::string(data_size, 'a'),
                                 ::rand() % 2 == 0 ? "" : "123");
      r.add(ur);
    }
    size_t predicted = r.accurate_size();
    std::string serialized = r.serialize();
    BOOST_CHECK_EQUAL(predicted, serialized.length());
  }
}

// Check that the estimated_size() gives a sane estimate
BOOST_AUTO_TEST_CASE(EstimatedSize) {
  for (int i = 0; i < 50; i++) {
    aws::kinesis::core::KinesisRecord r;
    uint64_t num_records = aws::utils::random_int(2, 512);

    // non repeated partition keys
    for (uint64_t j = 0; j < num_records; j++) {
      uint64_t key_size = aws::utils::random_int(1, 256);
      uint64_t data_size = aws::utils::random_int(1, 64 * 1024);
      auto ur = aws::kinesis::test::make_user_record(std::string(key_size, 'a'),
                                 std::string(data_size, 'a'),
                                 ::rand() % 2 == 0 ? "" : "123");
      r.add(ur);
    }

    // repeated partition keys
    uint64_t key_size = aws::utils::random_int(1, 256);
    for (uint64_t j = 0; j < num_records; j++) {
      uint64_t data_size = aws::utils::random_int(1, 64 * 1024);
      auto ur = aws::kinesis::test::make_user_record(std::string(key_size, 'a'),
                                 std::string(data_size, 'a'),
                                 ::rand() % 2 == 0 ? "" : "123");
      r.add(ur);
    }

    // small keys small data
    for (uint64_t j = 0; j < num_records; j++) {
      auto ur = aws::kinesis::test::make_user_record(std::string(2, 'a'),
                                 std::string(2, 'a'),
                                 ::rand() % 2 == 0 ? "" : "123");
      r.add(ur);
    }

    size_t estimated = r.estimated_size();
    std::string serialized = r.serialize();

    double diff = (double) serialized.length() - estimated;
    double percentage_diff = diff / serialized.length() * 100;
    percentage_diff *= percentage_diff < 0 ? -1 : 1;

    std::stringstream ss;
    ss << "Estimated size should be within 1 percent or 32 bytes of actual "
       << "size, estimate was " << estimated << ", actual size was "
       << serialized.length() << " (" << percentage_diff << "% difference)";
    BOOST_CHECK_MESSAGE(percentage_diff < 1 || diff < 32, ss.str());
  }
}

// aws::kinesis::test::verify correct behavior when container is empty
BOOST_AUTO_TEST_CASE(Empty) {
  aws::kinesis::core::KinesisRecord r;
  BOOST_CHECK_EQUAL(0, r.accurate_size());
  BOOST_CHECK_EQUAL(0, r.estimated_size());

  try {
    r.serialize();
    BOOST_FAIL(
        "Calling serialize on empty KinesisRecord should cause exception");
  } catch (std::exception e) {
    // expected
  }
}

// Test that clearing works correctly
BOOST_AUTO_TEST_CASE(Clearing) {
  int N = 10;

  std::vector<std::shared_ptr<aws::kinesis::core::UserRecord>> user_records;
  aws::kinesis::core::KinesisRecord r;
  for (int i = 0; i < N; i++) {
    auto ur = aws::kinesis::test::make_user_record();
    user_records.push_back(ur);
    r.add(ur);
  }

  aws::kinesis::test::verify(user_records, r);

  for (int i = 0; i < 5; i++) {
    r.clear();
    user_records.clear();

    for (int i = 0; i < N; i++) {
      auto ur = aws::kinesis::test::make_user_record();
      user_records.push_back(ur);
      r.add(ur);
    }

    aws::kinesis::test::verify(user_records, r);
  }
}

// Test that deadlines are correctly set
BOOST_AUTO_TEST_CASE(Deadlines) {
  // The nearest deadline should always be kept
  {
    aws::kinesis::core::KinesisRecord r;
    auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < 10; i++) {
      auto ur = aws::kinesis::test::make_user_record();
      ur->set_deadline(start + std::chrono::milliseconds(i * 100));
      ur->set_expiration(start + std::chrono::milliseconds(i * 100));
      r.add(ur);
    }
    BOOST_CHECK(r.deadline() == start);
    BOOST_CHECK(r.expiration() == start);
  }

  // If a nearer deadline comes in, it should override the previous
  {
    aws::kinesis::core::KinesisRecord r;
    auto earlier = std::chrono::steady_clock::now();
    auto later = earlier + std::chrono::milliseconds(500);
    {
      auto ur = aws::kinesis::test::make_user_record();
      ur->set_deadline(later);
      r.add(ur);
      BOOST_CHECK(r.deadline() == later);
    }
    {
      auto ur = aws::kinesis::test::make_user_record();
      ur->set_deadline(earlier);
      r.add(ur);
      BOOST_CHECK(r.deadline() == earlier);
    }
    // Removing the last added record should restore the previous deadline
    r.remove_last();
    BOOST_CHECK(r.deadline() == later);
  }
}

// The throughput of calling add() and estimated_size() on each UserRecord.
// 1.6 M/s on mac, at time of writing.
BOOST_AUTO_TEST_CASE(AddAndEstimateThroughput) {
  volatile size_t n = 0;

  size_t N = 1000000;

  std::vector<std::shared_ptr<aws::kinesis::core::UserRecord>> v;
  for (size_t i = 0; i < N; i++) {
    v.push_back(aws::kinesis::test::make_user_record("pk", "data", "123"));
  }

  aws::kinesis::core::KinesisRecord r;

  auto start = std::chrono::high_resolution_clock::now();

  for (size_t i = 0; i < N; i++) {
    r.add(v[i]);
    if (r.estimated_size() < 100000) {
      n++;
    }
  }

  auto nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::high_resolution_clock::now() - start).count();
  double seconds = nanos / 1e9;
  LOG(info) << "KinesisRecord add and estimate rate: " << N / seconds << " rps";
}

BOOST_AUTO_TEST_SUITE_END()
