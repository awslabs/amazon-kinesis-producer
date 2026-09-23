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

#include <aws/kinesis/core/put_records_context.h>
#include <aws/kinesis/core/test/test_utils.h>

namespace {

// Wraps a single user record in a KinesisRecord (the solo, non-aggregated form
// the Pipeline produces for AUTO and UNKNOWN streams). service_routed mirrors
// what the Pipeline freezes onto the record at routing time, which is what
// to_sdk_request() now keys off (not a context-level flag).
std::shared_ptr<aws::kinesis::core::KinesisRecord>
make_solo_record(const std::shared_ptr<aws::kinesis::core::UserRecord>& ur,
                 bool service_routed) {
  auto kr = std::make_shared<aws::kinesis::core::KinesisRecord>();
  kr->add(ur);
  kr->set_service_routed(service_routed);
  return kr;
}

auto make_ctx(const std::shared_ptr<aws::kinesis::core::KinesisRecord>& kr) {
  std::vector<std::shared_ptr<aws::kinesis::core::KinesisRecord>> krs{kr};
  return std::make_shared<aws::kinesis::core::PutRecordsContext>(
      "myStream", "", "", krs);
}

} // namespace

BOOST_AUTO_TEST_SUITE(PutRecordsContext)

// AUTO stream: ExplicitHashKey must not be set on the request entry.
BOOST_AUTO_TEST_CASE(AutoStream_NoExplicitHashKey) {
  auto kr = make_solo_record(aws::kinesis::test::make_user_record("pk", "data"),
                             /*service_routed=*/true);
  auto req = make_ctx(kr)->to_sdk_request();

  BOOST_REQUIRE_EQUAL(req.GetRecords().size(), 1u);
  BOOST_CHECK(!req.GetRecords()[0].ExplicitHashKeyHasBeenSet());
}

// AUTO stream with an empty (absent) partition key: PartitionKey must be omitted.
BOOST_AUTO_TEST_CASE(AutoStream_NullPK_PKOmitted) {
  auto kr = make_solo_record(aws::kinesis::test::make_user_record_no_pk("data"),
                             /*service_routed=*/true);
  auto req = make_ctx(kr)->to_sdk_request();

  BOOST_REQUIRE_EQUAL(req.GetRecords().size(), 1u);
  BOOST_CHECK(!req.GetRecords()[0].PartitionKeyHasBeenSet());
  BOOST_CHECK(!req.GetRecords()[0].ExplicitHashKeyHasBeenSet());
}

// UNKNOWN stream (service_routed, the safe default for an undiscovered stream)
// with a null PK: the entry must omit both PartitionKey and ExplicitHashKey. The
// pre-fix bug sent SetPartitionKey("") + SetExplicitHashKey("0"), which a real
// non-AUTO endpoint rejects with a ValidationException.
BOOST_AUTO_TEST_CASE(UnknownStream_NullPK_PKAndEHKOmitted) {
  auto kr = make_solo_record(aws::kinesis::test::make_user_record_no_pk("data"),
                             /*service_routed=*/true);
  auto req = make_ctx(kr)->to_sdk_request();

  BOOST_REQUIRE_EQUAL(req.GetRecords().size(), 1u);
  BOOST_CHECK(!req.GetRecords()[0].PartitionKeyHasBeenSet());
  BOOST_CHECK(!req.GetRecords()[0].ExplicitHashKeyHasBeenSet());
}

// AUTO stream with a user-provided partition key: PartitionKey is set (stored
// by the service but not used for routing), EHK still omitted.
BOOST_AUTO_TEST_CASE(AutoStream_NonNullPK_PKSet) {
  auto kr = make_solo_record(aws::kinesis::test::make_user_record("mypk", "data"),
                             /*service_routed=*/true);
  auto req = make_ctx(kr)->to_sdk_request();

  BOOST_REQUIRE_EQUAL(req.GetRecords().size(), 1u);
  BOOST_CHECK(req.GetRecords()[0].PartitionKeyHasBeenSet());
  BOOST_CHECK_EQUAL(req.GetRecords()[0].GetPartitionKey(), "mypk");
  BOOST_CHECK(!req.GetRecords()[0].ExplicitHashKeyHasBeenSet());
}

// Regression: USER_PARTITION_KEY stream (not service-routed) still sets both
// PartitionKey and ExplicitHashKey.
BOOST_AUTO_TEST_CASE(NonAuto_EHK_Present) {
  auto kr = make_solo_record(aws::kinesis::test::make_user_record("pk", "data"),
                             /*service_routed=*/false);
  auto req = make_ctx(kr)->to_sdk_request();

  BOOST_REQUIRE_EQUAL(req.GetRecords().size(), 1u);
  BOOST_CHECK(req.GetRecords()[0].PartitionKeyHasBeenSet());
  BOOST_CHECK(req.GetRecords()[0].ExplicitHashKeyHasBeenSet());
}

BOOST_AUTO_TEST_SUITE_END()
