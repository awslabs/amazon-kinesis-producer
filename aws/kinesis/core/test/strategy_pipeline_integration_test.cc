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

// Integration test for the StreamStrategyManager <-> Pipeline seam, wired the
// same way KinesisProducer::create_pipeline wires them: the Pipeline's strategy
// getter queries a real StreamStrategyManager, and the Pipeline's wrong-shard
// callback feeds StreamStrategyManager::record_wrong_shard. Unit tests cover
// each side in isolation; this verifies the manager's answer actually drives
// pipeline routing and that wrong-shard events reach the manager.

#include <chrono>
#include <thread>

#include <boost/test/unit_test.hpp>

#include <aws/kinesis/core/pipeline.h>
#include <aws/kinesis/core/stream_strategy_manager.h>
#include <aws/kinesis/core/test/pipeline_test_support.h>
#include <aws/kinesis/core/test/test_utils.h>
#include <aws/utils/io_service_executor.h>

namespace {

using aws::kinesis::core::StreamStrategy;
using aws::kinesis::core::StreamStrategyManager;
using aws::kinesis::core::PutRecordsContext;
using aws::kinesis::test::kMockShardId;
using aws::kinesis::test::MockShardMap;
using aws::kinesis::test::leaked_pipelines;

struct Harness {
  std::shared_ptr<aws::utils::IoServiceExecutor> executor;
  std::shared_ptr<StreamStrategyManager> manager;
  std::shared_ptr<aws::kinesis::core::Pipeline> pipeline;
  std::shared_ptr<std::vector<std::shared_ptr<aws::kinesis::core::UserRecord>>> sent;
  // KinesisRecords as assembled for sending, so tests can inspect the per-record
  // service_routed() flag that drives request assembly.
  std::shared_ptr<std::vector<std::shared_ptr<aws::kinesis::core::KinesisRecord>>> sent_krs;
  // UserRecords the pipeline finished directly (e.g. the null-PK gate failing a
  // record before it is ever sent), so tests can inspect the failure attempt.
  std::shared_ptr<std::vector<std::shared_ptr<aws::kinesis::core::UserRecord>>> finished;
};

// Builds a real manager (with the given default + scripted resolver) wired to a
// real Pipeline exactly as create_pipeline does.
Harness make_harness(
    StreamStrategy default_strategy,
    StreamStrategyManager::StrategyResolver resolver,
    StreamStrategyManager::Timing timing = StreamStrategyManager::Timing()) {
  Harness h;
  h.executor = std::make_shared<aws::utils::IoServiceExecutor>(1);
  h.manager = std::make_shared<StreamStrategyManager>(
      h.executor, default_strategy, std::move(resolver),
      [](const std::string&, StreamStrategy) {}, timing);

  auto config = std::make_shared<aws::kinesis::core::Configuration>();
  auto kinesis_client = std::make_shared<Aws::Kinesis::KinesisClient>(
      Aws::Client::ClientConfiguration());
  auto metrics_manager = std::make_shared<aws::metrics::NullMetricsManager>();
  h.sent = std::make_shared<
      std::vector<std::shared_ptr<aws::kinesis::core::UserRecord>>>();

  h.sent_krs = std::make_shared<
      std::vector<std::shared_ptr<aws::kinesis::core::KinesisRecord>>>();
  h.finished = std::make_shared<
      std::vector<std::shared_ptr<aws::kinesis::core::UserRecord>>>();

  auto manager = h.manager;
  auto sent = h.sent;
  auto sent_krs = h.sent_krs;
  auto finished = h.finished;
  h.pipeline = std::make_shared<aws::kinesis::core::Pipeline>(
      "us-east-1", "myStream", config, h.executor, kinesis_client,
      metrics_manager,
      [finished](auto& ur) { finished->push_back(ur); },
      [](const std::string&) { return std::string(); },
      // strategy getter -> real manager (same as create_pipeline)
      [manager](const std::string& s) { return manager->get_strategy(s); },
      std::make_shared<MockShardMap>(),
      // send sink: capture the user records and the assembled KinesisRecords
      [sent, sent_krs](const std::shared_ptr<PutRecordsContext>& prc) {
        for (auto& kr : prc->get_records()) {
          sent_krs->push_back(kr);
          for (auto& ur : kr->items()) {
            sent->push_back(ur);
          }
        }
      },
      // wrong-shard callback -> real manager (same as create_pipeline)
      [manager](const std::string& s) { manager->record_wrong_shard(s); });
  leaked_pipelines().push_back(h.pipeline);
  return h;
}

boost::optional<StreamStrategy> always(StreamStrategy s) {
  return s;
}

} // namespace

BOOST_AUTO_TEST_SUITE(StrategyPipelineIntegration)

// Manager resolved to USER_PARTITION_KEY -> pipeline aggregates (predicted_shard
// set from the shard map).
BOOST_AUTO_TEST_CASE(ManagerUserPK_PipelineAggregates) {
  auto h = make_harness(StreamStrategy::USER_PARTITION_KEY,
                        [](const std::string&) { return always(StreamStrategy::USER_PARTITION_KEY); });
  // Confirm the manager actually reports UPK for this stream.
  BOOST_REQUIRE(h.manager->get_strategy("myStream") == StreamStrategy::USER_PARTITION_KEY);

  auto ur = aws::kinesis::test::make_user_record("pk", "data");
  h.pipeline->put(ur);

  BOOST_REQUIRE(ur->predicted_shard());
  BOOST_CHECK_EQUAL(*ur->predicted_shard(), kMockShardId);
}

// Manager resolved to AUTO -> pipeline routes solo (predicted_shard cleared).
BOOST_AUTO_TEST_CASE(ManagerAuto_PipelineSolo) {
  auto h = make_harness(StreamStrategy::AUTO,
                        [](const std::string&) { return always(StreamStrategy::AUTO); });
  BOOST_REQUIRE(h.manager->get_strategy("myStream") == StreamStrategy::AUTO);

  auto ur = aws::kinesis::test::make_user_record_no_pk("data");
  h.pipeline->put(ur);

  BOOST_CHECK(!ur->predicted_shard());
}

// No default + a resolver that returns UNKNOWN (unresolved): the manager stays
// UNKNOWN, so the pipeline routes solo (the safe default for an undiscovered
// stream).
BOOST_AUTO_TEST_CASE(Undiscovered_PipelineSolo) {
  auto h = make_harness(StreamStrategy::UNKNOWN,
                        [](const std::string&) { return boost::optional<StreamStrategy>(); });
  BOOST_REQUIRE(h.manager->get_strategy("myStream") == StreamStrategy::UNKNOWN);

  auto ur = aws::kinesis::test::make_user_record("pk", "data");
  h.pipeline->put(ur);

  BOOST_CHECK(!ur->predicted_shard());
}

// A strategy flip between routing and assembly must NOT change how an
// already-routed record is sent. The record is routed while the
// manager reports AUTO, so it is wrapped solo and frozen service_routed=true.
// We then flip the manager to USER_PARTITION_KEY before the record is flushed
// out for assembly. The assembled KinesisRecord must still report
// service_routed()==true: assembly keys off the frozen per-record flag, never a
// re-sampled live strategy.
BOOST_AUTO_TEST_CASE(StrategyFlipMidFlight_RecordKeepsFrozenAssembly) {
  // Default AUTO seeds the initial strategy, so the record routes while AUTO.
  // The resolver returns USER_PARTITION_KEY, so the explicit refresh_one below
  // flips the live strategy after the record has already been routed.
  auto h = make_harness(
      StreamStrategy::AUTO,
      [](const std::string&) { return always(StreamStrategy::USER_PARTITION_KEY); });
  BOOST_REQUIRE(h.manager->get_strategy("myStream") == StreamStrategy::AUTO);

  // Route the record while AUTO: solo-wrapped, service_routed frozen true.
  auto ur = aws::kinesis::test::make_user_record_no_pk("data");
  h.pipeline->put(ur);
  BOOST_CHECK(!ur->predicted_shard());

  // Flip the live strategy to USER_PARTITION_KEY.
  h.manager->refresh_one("myStream");
  BOOST_REQUIRE(h.manager->get_strategy("myStream") ==
                StreamStrategy::USER_PARTITION_KEY);

  // Flush so the already-routed record is assembled and handed to the sink.
  h.pipeline->flush();
  for (int i = 0; i < 200 && h.sent_krs->empty(); i++) {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }

  BOOST_REQUIRE_EQUAL(h.sent_krs->size(), 1u);
  // Frozen at routing time despite the post-routing flip to UPK.
  BOOST_CHECK(h.sent_krs->front()->service_routed());
}

// A record with no partition key that is routed while the manager reports
// USER_PARTITION_KEY must be failed, not aggregated and sent. This is the corner
// case where a record submitted under a (wrong) AUTO assumption is retried after
// discovery has corrected the stream to USER_PARTITION_KEY: without the gate it
// would aggregate (acquiring the container's synthetic partition key) and land on
// the stream. The pipeline must instead finish it with an InvalidPartitionKey
// failure and send nothing.
BOOST_AUTO_TEST_CASE(NullPK_OnUserPKStream_FailedNotSent) {
  auto h = make_harness(
      StreamStrategy::USER_PARTITION_KEY,
      [](const std::string&) { return always(StreamStrategy::USER_PARTITION_KEY); });
  BOOST_REQUIRE(h.manager->get_strategy("myStream") ==
                StreamStrategy::USER_PARTITION_KEY);

  auto ur = aws::kinesis::test::make_user_record_no_pk("data");
  h.pipeline->put(ur);

  // Failed directly, before any send: nothing aggregated, nothing sent.
  BOOST_REQUIRE_EQUAL(h.finished->size(), 1u);
  BOOST_CHECK(h.sent->empty());
  BOOST_CHECK(h.sent_krs->empty());

  // The failure carries the InvalidPartitionKey code and the Java-matching text.
  auto& attempts = h.finished->front()->attempts();
  BOOST_REQUIRE(!attempts.empty());
  const auto& last = attempts.back();
  BOOST_CHECK(!last);  // Attempt::operator bool() is false when errored
  BOOST_CHECK_EQUAL(last.error_code(), "InvalidPartitionKey");
  BOOST_CHECK_EQUAL(last.error_message(), "partitionKey cannot be null");
}

// No default + discovery permanently fails (e.g. a version bump whose role lacks
// kinesis:DescribeStreamSummary): the first-write blocking discovery exhausts
// its attempts, the manager falls back to LEGACY_AGGREGATE, and the real
// pipeline keeps aggregating -- the backward-compatibility guarantee end-to-end.
BOOST_AUTO_TEST_CASE(DiscoveryFails_FallbackAggregates) {
  StreamStrategyManager::Timing timing;
  timing.blocking_backoff = std::chrono::milliseconds(1);  // keep the test fast
  auto h = make_harness(
      StreamStrategy::UNKNOWN,
      [](const std::string&) { return boost::optional<StreamStrategy>(); },
      timing);

  // First-write discovery: every attempt fails -> fall back to LEGACY_AGGREGATE.
  BOOST_REQUIRE(h.manager->get_or_discover("myStream") ==
                StreamStrategy::LEGACY_AGGREGATE);
  BOOST_REQUIRE(h.manager->get_strategy("myStream") ==
                StreamStrategy::LEGACY_AGGREGATE);

  auto ur = aws::kinesis::test::make_user_record("pk", "data");
  h.pipeline->put(ur);

  // Aggregates like a USER_PARTITION_KEY stream (predicted shard set), rather
  // than silently dropping to solo.
  BOOST_REQUIRE(ur->predicted_shard());
  BOOST_CHECK_EQUAL(*ur->predicted_shard(), kMockShardId);
}

BOOST_AUTO_TEST_SUITE_END()
