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

// Wire-contract tests for the protobuf changes made for RecordDistributionStrategy=AUTO.
// The Java and C++ layers are built and unit-tested separately against their own
// assumptions about these messages; these tests verify both ends agree on the wire
// by round-tripping through serialize/parse (the same path IPC uses) and through the
// production Configuration::transfer_from_protobuf_msg.

#include <boost/test/unit_test.hpp>

#include <aws/kinesis/protobuf/messages.pb.h>
#include <aws/kinesis/core/configuration.h>

namespace {

// Round-trips a Message through serialized bytes, as the IPC layer does.
aws::kinesis::protobuf::Message round_trip(
    const aws::kinesis::protobuf::Message& in) {
  std::string bytes = in.SerializeAsString();
  aws::kinesis::protobuf::Message out;
  BOOST_REQUIRE(out.ParseFromString(bytes));
  return out;
}

} // namespace

BOOST_AUTO_TEST_SUITE(ProtoContract)

// PutRecord.partition_key is now optional: a record sent without one must
// round-trip with the field reported absent (not an empty-but-present string),
// so the C++ UserRecord can distinguish "no PK" and skip hash computation.
BOOST_AUTO_TEST_CASE(PutRecord_NoPartitionKey) {
  aws::kinesis::protobuf::Message m;
  m.set_id(1);
  auto* pr = m.mutable_put_record();
  pr->set_stream_name("myStream");
  pr->set_data("data");
  // partition_key intentionally not set

  auto out = round_trip(m);
  BOOST_REQUIRE(out.has_put_record());
  BOOST_CHECK(!out.put_record().has_partition_key());
  BOOST_CHECK(out.put_record().partition_key().empty());
}

// A PutRecord WITH a partition key still round-trips with it present.
BOOST_AUTO_TEST_CASE(PutRecord_WithPartitionKey) {
  aws::kinesis::protobuf::Message m;
  m.set_id(1);
  auto* pr = m.mutable_put_record();
  pr->set_stream_name("myStream");
  pr->set_partition_key("pk");
  pr->set_data("data");

  auto out = round_trip(m);
  BOOST_REQUIRE(out.has_put_record());
  BOOST_CHECK(out.put_record().has_partition_key());
  BOOST_CHECK_EQUAL(out.put_record().partition_key(), "pk");
}

// StreamStrategyUpdate is the reverse (daemon -> Java) message. Verify it lands
// in the oneof as stream_strategy_update and its fields survive the wire. This
// is exactly the message KinesisProducer::send_strategy_update_to_java builds.
BOOST_AUTO_TEST_CASE(StreamStrategyUpdate_RoundTrip) {
  aws::kinesis::protobuf::Message m;
  m.set_id(7);
  auto* update = m.mutable_stream_strategy_update();
  update->set_stream_name("myStream");
  update->set_record_distribution_strategy("AUTO");

  auto out = round_trip(m);
  BOOST_REQUIRE(out.has_stream_strategy_update());
  BOOST_CHECK(!out.has_put_record_result());
  BOOST_CHECK(!out.has_stream_metadata());
  BOOST_CHECK_EQUAL(out.stream_strategy_update().stream_name(), "myStream");
  BOOST_CHECK_EQUAL(out.stream_strategy_update().record_distribution_strategy(),
                    "AUTO");
}

// StreamMetadata's new record_distribution_strategy field round-trips and is
// independent of stream_id.
BOOST_AUTO_TEST_CASE(StreamMetadata_RecordDistributionStrategy) {
  aws::kinesis::protobuf::Message m;
  m.set_id(1);
  auto* meta = m.mutable_stream_metadata();
  meta->set_stream_name("myStream");
  meta->set_record_distribution_strategy("USER_PARTITION_KEY");

  auto out = round_trip(m);
  BOOST_REQUIRE(out.has_stream_metadata());
  BOOST_CHECK(out.stream_metadata().has_record_distribution_strategy());
  BOOST_CHECK_EQUAL(out.stream_metadata().record_distribution_strategy(),
                    "USER_PARTITION_KEY");
  BOOST_CHECK(!out.stream_metadata().has_stream_id());
}

// The two new Configuration fields flow Java -> C++ through the production
// transfer_from_protobuf_msg path with their values intact.
BOOST_AUTO_TEST_CASE(Configuration_NewFields) {
  aws::kinesis::protobuf::Message m;
  m.set_id(1);
  auto* c = m.mutable_configuration();
  c->set_describe_stream_summary_interval(60000);
  c->set_record_distribution_strategy_default("AUTO");

  auto out = round_trip(m);
  aws::kinesis::core::Configuration config;
  config.transfer_from_protobuf_msg(out);

  BOOST_CHECK_EQUAL(config.describe_stream_summary_interval(), 60000);
  BOOST_CHECK_EQUAL(config.record_distribution_strategy_default(), "AUTO");
}

// When the new Configuration fields are absent, the C++ Configuration keeps its
// defaults (5-minute interval, no default strategy).
BOOST_AUTO_TEST_CASE(Configuration_NewFields_DefaultsWhenAbsent) {
  aws::kinesis::protobuf::Message m;
  m.set_id(1);
  m.mutable_configuration();  // no new fields set

  auto out = round_trip(m);
  aws::kinesis::core::Configuration config;
  config.transfer_from_protobuf_msg(out);

  BOOST_CHECK_EQUAL(config.describe_stream_summary_interval(), 300000);
  BOOST_CHECK(config.record_distribution_strategy_default().empty());
}

BOOST_AUTO_TEST_SUITE_END()
