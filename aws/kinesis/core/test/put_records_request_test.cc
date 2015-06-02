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

#include <stack>

#include <boost/test/unit_test.hpp>

#include <glog/logging.h>

#include <aws/kinesis/core/put_records_request.h>
#include <aws/utils/utils.h>
#include <aws/utils/json.h>
#include <aws/kinesis/core/test/test_utils.h>

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

void verify(aws::kinesis::core::PutRecordsRequest& prr,
            const KinesisRecordSharedPtrVector& krs) {
  try {
    auto json = aws::utils::Json(prr.serialize());

    BOOST_CHECK_EQUAL((std::string) json["StreamName"], prr.stream());

    size_t i = 0;
    for (; i < json["Records"].size(); i++) {
      auto& kr = *krs[i];
      auto record = json["Records"][i];

      BOOST_CHECK_EQUAL((std::string) record["PartitionKey"],
                        kr.partition_key());
      BOOST_CHECK_EQUAL((std::string) record["ExplicitHashKey"],
                        kr.explicit_hash_key());
      BOOST_CHECK_EQUAL((std::string) record["Data"],
                        aws::utils::base64_encode(kr.serialize()));
    }

    BOOST_CHECK_EQUAL(i, krs.size());
  } catch (const std::exception& e) {
    LOG(FATAL) << "Could not parse json, data was:\n" << prr.serialize();
  }
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

BOOST_AUTO_TEST_CASE(Serialization) {
  for (int i = 1; i < 200; i += 10) {
    KinesisRecordSharedPtrVector v;
    aws::kinesis::core::PutRecordsRequest prr;
    for (int j = 0; j < i; j++) {
      auto kr = make_kinesis_record();
      prr.add(kr);
      v.push_back(kr);
    }
    for (int k = 0; i > 3 && k < 3; k++) {
      prr.remove_last();
      v.pop_back();
    }
    verify(prr, v);
  }
}

BOOST_AUTO_TEST_CASE(Throughput) {
  const size_t N = 50;

  aws::kinesis::core::PutRecordsRequest prr;
  for (size_t i = 0; i < 500; i++) {
    prr.add(make_kinesis_record(10500));
  }

  volatile size_t serialized_size = 0;

  auto start = std::chrono::high_resolution_clock::now();

  for (size_t i = 0; i < N; i++) {
    serialized_size = prr.serialize().length();
  }

  auto nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::high_resolution_clock::now() - start).count();
  double seconds = nanos / 1e9;
  double mbs = (double) serialized_size / 1024 / 1024;
  LOG(INFO) << "PutRecordsRequest serialization rate (500 records, "
            << mbs << " MB json per req): " << N / seconds << " rps, "
            << N * mbs / seconds << " MB/s";
}

BOOST_AUTO_TEST_SUITE_END()
