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

// Tests the Pipeline's binary aggregate/solo routing decision:
//   - confirmed USER_PARTITION_KEY -> shard-based Aggregator (predicted_shard set)
//   - AUTO or UNKNOWN              -> solo path (predicted_shard cleared)
//
// The routing decision sets/clears predicted_shard on the UserRecord
// synchronously inside put() (the Aggregator sets it from the injected shard map
// for USER_PARTITION_KEY; the solo path calls reset_predicted_shard()), so we
// observe ur->predicted_shard() directly after put() with no flush-timing
// dependency. The injected PutRecordsHandler seam absorbs the solo send so the
// AUTO/UNKNOWN paths never reach the real Kinesis client.

#include <boost/test/unit_test.hpp>

#include <aws/kinesis/core/pipeline.h>
#include <aws/kinesis/core/test/pipeline_test_support.h>
#include <aws/kinesis/core/test/test_utils.h>
#include <aws/utils/io_service_executor.h>

namespace {

using aws::kinesis::core::StreamStrategy;
using aws::kinesis::core::PutRecordsContext;
using aws::kinesis::test::kMockShardId;
using aws::kinesis::test::MockShardMap;
using aws::kinesis::test::leaked_pipelines;

using aws::kinesis::core::UserRecord;

auto make_pipeline(
    StreamStrategy strategy,
    aws::kinesis::core::Retrier::UserRecordCallback finish_cb =
        [](auto&) {}) {
  auto config = std::make_shared<aws::kinesis::core::Configuration>();

  auto executor = std::make_shared<aws::utils::IoServiceExecutor>(1);
  auto kinesis_client = std::make_shared<Aws::Kinesis::KinesisClient>(
      Aws::Client::ClientConfiguration());
  auto metrics_manager = std::make_shared<aws::metrics::NullMetricsManager>();

  auto pipeline = std::make_shared<aws::kinesis::core::Pipeline>(
      "us-east-1",
      "myStream",
      config,
      executor,
      kinesis_client,
      metrics_manager,
      std::move(finish_cb),                               // finish cb
      [](const std::string&) { return std::string(); },  // stream id getter
      [strategy](const std::string&) { return strategy; },
      std::make_shared<MockShardMap>(),
      // Absorb any solo send so AUTO/UNKNOWN never reach the real client.
      [](const std::shared_ptr<PutRecordsContext>&) {});
  leaked_pipelines().push_back(pipeline);
  return pipeline;
}

} // namespace

BOOST_AUTO_TEST_SUITE(PipelineAuto)

// USER_PARTITION_KEY: record goes through the Aggregator, which sets
// predicted_shard from the shard map. It stays buffered (not sent).
BOOST_AUTO_TEST_CASE(UserPartitionKey_Aggregates) {
  auto pipeline = make_pipeline(StreamStrategy::USER_PARTITION_KEY);
  auto ur = aws::kinesis::test::make_user_record("pk", "data");

  pipeline->put(ur);

  BOOST_REQUIRE(ur->predicted_shard());
  BOOST_CHECK_EQUAL(*ur->predicted_shard(), kMockShardId);
}

// AUTO: record goes through the solo path; predicted_shard is cleared.
BOOST_AUTO_TEST_CASE(AutoStream_NoAggregation) {
  auto pipeline = make_pipeline(StreamStrategy::AUTO);
  auto ur = aws::kinesis::test::make_user_record("pk", "data");

  pipeline->put(ur);

  BOOST_CHECK(!ur->predicted_shard());
}

// UNKNOWN: behaves like AUTO for record handling (solo, no predicted shard).
BOOST_AUTO_TEST_CASE(UnknownStrategy_NoAggregation) {
  auto pipeline = make_pipeline(StreamStrategy::UNKNOWN);
  auto ur = aws::kinesis::test::make_user_record("pk", "data");

  pipeline->put(ur);

  BOOST_CHECK(!ur->predicted_shard());
}

// AUTO with no partition key (the full AUTO shape): still solo, no shard.
BOOST_AUTO_TEST_CASE(AutoStream_EmptyPK_NoAggregation) {
  auto pipeline = make_pipeline(StreamStrategy::AUTO);
  auto ur = aws::kinesis::test::make_user_record_no_pk("data");

  pipeline->put(ur);

  BOOST_CHECK(!ur->predicted_shard());
  BOOST_CHECK(ur->partition_key().empty());
}

// USER_PARTITION_KEY with an empty partition key: the record must be failed at
// route time (not aggregated), so an empty-PK record can never land on a
// USER_PARTITION_KEY stream. This covers the case where a record was submitted
// while the strategy still looked like AUTO and discovery later resolved the
// stream to USER_PARTITION_KEY.
BOOST_AUTO_TEST_CASE(UserPartitionKey_EmptyPK_Failed) {
  std::shared_ptr<UserRecord> finished;
  auto pipeline = make_pipeline(
      StreamStrategy::USER_PARTITION_KEY,
      [&finished](auto& ur) { finished = ur; });
  auto ur = aws::kinesis::test::make_user_record_no_pk("data");

  pipeline->put(ur);

  // Failed synchronously at route time, not aggregated (no predicted shard).
  BOOST_REQUIRE(finished);
  BOOST_CHECK_EQUAL(finished.get(), ur.get());
  BOOST_CHECK(!ur->predicted_shard());
  BOOST_REQUIRE_EQUAL(ur->attempts().size(), 1u);
  const auto& attempt = ur->attempts().back();
  BOOST_CHECK(!attempt);  // errored attempt
  BOOST_CHECK_EQUAL(attempt.error_code(), "InvalidPartitionKey");
  BOOST_CHECK_EQUAL(attempt.error_message(), "partitionKey cannot be null");
}

// LEGACY_AGGREGATE (backward-compat fallback used when discovery fails with no
// configured default): aggregates just like USER_PARTITION_KEY, so a version
// bump missing the DescribeStreamSummary permission keeps aggregating.
BOOST_AUTO_TEST_CASE(LegacyAggregate_Aggregates) {
  auto pipeline = make_pipeline(StreamStrategy::LEGACY_AGGREGATE);
  auto ur = aws::kinesis::test::make_user_record("pk", "data");

  pipeline->put(ur);

  BOOST_REQUIRE(ur->predicted_shard());
  BOOST_CHECK_EQUAL(*ur->predicted_shard(), kMockShardId);
}

// LEGACY_AGGREGATE with an empty partition key: unlike a confirmed
// USER_PARTITION_KEY, the record is NOT failed at route time. The fallback
// preserves pre-AUTO behavior (empty-PK records were accepted) since the
// stream's real strategy is still unknown; it still aggregates.
BOOST_AUTO_TEST_CASE(LegacyAggregate_EmptyPK_NotFailed) {
  std::shared_ptr<UserRecord> finished;
  auto pipeline = make_pipeline(
      StreamStrategy::LEGACY_AGGREGATE,
      [&finished](auto& ur) { finished = ur; });
  auto ur = aws::kinesis::test::make_user_record_no_pk("data");

  pipeline->put(ur);

  BOOST_CHECK(!finished);            // not failed at route time
  BOOST_REQUIRE(ur->predicted_shard());  // took the aggregating path
}

BOOST_AUTO_TEST_SUITE_END()
