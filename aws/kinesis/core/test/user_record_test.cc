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

#include <iomanip>
#include <sstream>

#include <boost/test/unit_test.hpp>

#include <aws/utils/logging.h>

#include <aws/kinesis/core/user_record.h>

namespace {

std::string uint128_to_hex(boost::multiprecision::uint128_t x) {
  std::stringstream ss;
  ss << std::hex << x;
  return ss.str();
}

std::string uint128_to_decimal(boost::multiprecision::uint128_t x) {
  std::stringstream ss;
  ss << x;
  return ss.str();
}

const uint64_t kDefaultId = 1234567;
const char* kDefaultData = "hello world";
const char* kDefaultPartitionKey = "abcd";
const char* kDefaultStream = "myStream";

aws::kinesis::protobuf::Message make_put_record() {
  aws::kinesis::protobuf::Message m;
  m.set_id(kDefaultId);

  auto put_record = m.mutable_put_record();
  put_record->set_partition_key(kDefaultPartitionKey);
  put_record->set_data(kDefaultData);
  put_record->set_stream_name(kDefaultStream);

  return m;
}

void throughput_test(bool ehk) {
  size_t N = 250000;
  std::vector<aws::kinesis::protobuf::Message> messages;
  messages.reserve(N);
  for (size_t i = 0; i < N; i++) {
    auto m = make_put_record();
    m.mutable_put_record()->set_partition_key(std::string(256, 'a'));
    if (ehk) {
      m.mutable_put_record()->set_explicit_hash_key(std::string(38, '1'));
    }
    messages.push_back(m);
  }

  std::vector<std::unique_ptr<aws::kinesis::core::UserRecord>> v;
  v.reserve(N);

  std::chrono::high_resolution_clock::time_point start =
      std::chrono::high_resolution_clock::now();

  for (size_t i = 0; i < N; i++) {
    v.push_back(std::make_unique<aws::kinesis::core::UserRecord>(messages[i]));
  }

  std::chrono::high_resolution_clock::time_point end =
      std::chrono::high_resolution_clock::now();
  double seconds =
      std::chrono::duration_cast<std::chrono::nanoseconds>(end - start)
          .count() / 1e9;
  double rate = (double) N / seconds;
  LOG(info) << "Message conversion rate (" << (ehk ? "with" : "no") << " EHK): "
            << rate << " messages/s";
}

} // namespace

BOOST_AUTO_TEST_SUITE(UserRecord)

BOOST_AUTO_TEST_CASE(BasicConversion) {
  auto m = make_put_record();
  aws::kinesis::core::UserRecord ur(m);

  BOOST_CHECK_EQUAL(ur.stream(), kDefaultStream);
  BOOST_CHECK_EQUAL(ur.partition_key(), kDefaultPartitionKey);
  BOOST_CHECK_EQUAL(ur.data(), kDefaultData);
  BOOST_CHECK_EQUAL(ur.source_id(), kDefaultId);
}

// We should be using the md5 of the partition key when there is no explicit
// hash key.
BOOST_AUTO_TEST_CASE(HashKeyFromPartitionKey) {
  auto m = make_put_record();
  aws::kinesis::core::UserRecord ur(m);
  BOOST_CHECK_EQUAL(uint128_to_hex(ur.hash_key()),
                    "E2FC714C4727EE9395F324CD2E7F331F");
}

BOOST_AUTO_TEST_CASE(ExplicitHashKey) {
  std::string explicit_hash_key = "123456789";
  auto m = make_put_record();
  m.mutable_put_record()->set_explicit_hash_key(explicit_hash_key);

  aws::kinesis::core::UserRecord ur(m);
  BOOST_CHECK_EQUAL(uint128_to_decimal(ur.hash_key()), explicit_hash_key);
}

BOOST_AUTO_TEST_CASE(PutRecordResultFail) {
  auto m = make_put_record();
  aws::kinesis::core::UserRecord ur(m);

  auto now = std::chrono::steady_clock::now();

  aws::kinesis::core::Attempt a;
  a.set_start(now);
  a.set_end(now + std::chrono::milliseconds(5));
  a.set_error("code", "message");
  ur.add_attempt(std::move(a));

  aws::kinesis::core::Attempt b;
  b.set_start(now + std::chrono::milliseconds(11));
  b.set_end(now + std::chrono::milliseconds(18));
  b.set_error("code2", "message2");
  ur.add_attempt(std::move(b));

  aws::kinesis::protobuf::Message m2 = ur.to_put_record_result();
  BOOST_CHECK(m2.has_put_record_result());
  auto& prr = m2.put_record_result();

  BOOST_CHECK_EQUAL(prr.attempts_size(), 2);
  BOOST_CHECK_EQUAL(prr.success(), false);

  BOOST_CHECK_EQUAL(prr.attempts(0).success(), false);
  BOOST_CHECK_EQUAL(prr.attempts(0).error_code(), "code");
  BOOST_CHECK_EQUAL(prr.attempts(0).error_message(), "message");
  BOOST_CHECK_EQUAL(prr.attempts(0).delay(), 0);
  BOOST_CHECK_EQUAL(prr.attempts(0).duration(), 5);

  BOOST_CHECK_EQUAL(prr.attempts(1).success(), false);
  BOOST_CHECK_EQUAL(prr.attempts(1).error_code(), "code2");
  BOOST_CHECK_EQUAL(prr.attempts(1).error_message(), "message2");
  BOOST_CHECK_EQUAL(prr.attempts(1).delay(), 6);
  BOOST_CHECK_EQUAL(prr.attempts(1).duration(), 7);
}

BOOST_AUTO_TEST_CASE(PutRecordResultSuccess) {
  auto m = make_put_record();
  aws::kinesis::core::UserRecord ur(m);

  aws::kinesis::core::Attempt a;
  a.set_error("code", "message");
  ur.add_attempt(std::move(a));

  aws::kinesis::core::Attempt b;
  b.set_result("shard-0", "123456789");
  ur.add_attempt(std::move(b));

  aws::kinesis::protobuf::Message m2 = ur.to_put_record_result();
  BOOST_CHECK(m2.has_put_record_result());
  auto& prr = m2.put_record_result();

  BOOST_CHECK_EQUAL(prr.attempts_size(), 2);
  BOOST_CHECK_EQUAL(prr.success(), true);
  BOOST_CHECK_EQUAL(prr.shard_id(), "shard-0");
  BOOST_CHECK_EQUAL(prr.sequence_number(), "123456789");
}

BOOST_AUTO_TEST_CASE(HashKeyThroughputNoEHK) {
  throughput_test(false);
}

BOOST_AUTO_TEST_CASE(HashKeyThroughputWithEHK) {
  throughput_test(true);
}

BOOST_AUTO_TEST_SUITE_END()
