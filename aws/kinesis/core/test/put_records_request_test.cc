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

#include <stack>

#include <boost/test/unit_test.hpp>

#include <aws/kinesis/core/put_records_request.h>
#include <aws/kinesis/core/test/test_utils.h>
#include <aws/utils/logging.h>
#include <aws/utils/utils.h>

namespace {

using KinesisRecordSharedPtrVector =
    std::vector<std::shared_ptr<aws::kinesis::core::KinesisRecord>>;

auto make_kinesis_record(size_t min_serialized_size = 0) {
  auto kr = std::make_shared<aws::kinesis::core::KinesisRecord>();
  int num_user_records = 1 + (::rand() % 100);

  auto make_ur = [] {
    return aws::kinesis::test::make_user_record(
        std::to_string(::rand()),
        std::to_string(::rand()),
        std::to_string(::rand()),
        10000,
        "myStream");
  };

  for (int i = 0; i < num_user_records; i++) {
    kr->add(make_ur());
  }

  if (min_serialized_size > 0) {
    while (kr->accurate_size() < min_serialized_size) {
      kr->add(make_ur());
    }
  }

  return kr;
}

} //namespace

BOOST_AUTO_TEST_SUITE(PutRecordsRequest)

BOOST_AUTO_TEST_CASE(SizePrediction) {
  aws::kinesis::core::PutRecordsRequest prr;
  BOOST_CHECK_EQUAL(prr.accurate_size(), 0);
  BOOST_CHECK_EQUAL(prr.estimated_size(), prr.accurate_size());

  std::stack<size_t> sizes;
  sizes.push(0);
  const int N = 100;

  for (int i = 0; i < N; i++) {
    auto kr = make_kinesis_record();
    prr.add(kr);

    size_t expected_growth =
        kr->serialize().length() + kr->partition_key().length();
    size_t expected_size = sizes.top() + expected_growth;
    BOOST_CHECK_EQUAL(prr.accurate_size(), expected_size);
    BOOST_CHECK_EQUAL(prr.estimated_size(), prr.accurate_size());

    sizes.push(expected_size);
  }

  for (int i = 0; i < N; i++) {
    BOOST_CHECK_EQUAL(prr.accurate_size(), sizes.top());
    BOOST_CHECK_EQUAL(prr.estimated_size(), prr.accurate_size());

    sizes.pop();
    prr.remove_last();
  }

  BOOST_CHECK_EQUAL(prr.accurate_size(), 0);
  BOOST_CHECK_EQUAL(prr.estimated_size(), prr.accurate_size());
}

BOOST_AUTO_TEST_CASE(StreamName) {
  aws::kinesis::core::PutRecordsRequest prr;
  auto kr = make_kinesis_record();
  prr.add(kr);
  BOOST_CHECK_EQUAL(prr.stream(), "myStream");
}

BOOST_AUTO_TEST_SUITE_END()
