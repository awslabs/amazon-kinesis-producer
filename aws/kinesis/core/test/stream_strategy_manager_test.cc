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

#include <deque>
#include <mutex>

#include <boost/test/unit_test.hpp>

#include <aws/kinesis/core/stream_strategy_manager.h>

namespace {

using aws::kinesis::core::StreamStrategy;
using aws::kinesis::core::StreamStrategyManager;

// Executor that runs submit() inline and captures scheduled tasks so the test
// can run them deterministically (and inspect their delays) instead of waiting.
class FakeExecutor : public aws::utils::Executor {
 public:
  struct Scheduled {
    Func func;
    std::chrono::milliseconds delay;
  };

  class Handle : public aws::utils::ScheduledCallback {
   public:
    void cancel() override { cancelled_ = true; }
    bool completed() override { return completed_; }
    void reschedule(aws::utils::TimePoint at) override {}
    aws::utils::TimePoint expiration() override {
      return aws::utils::Clock::now();
    }
    bool cancelled_ = false;
    bool completed_ = false;
  };

  void submit(Func f) override { f(); }

  std::shared_ptr<aws::utils::ScheduledCallback>
  schedule(Func f, aws::utils::TimePoint at) override {
    scheduled.push_back({std::move(f), std::chrono::milliseconds(0)});
    return std::make_shared<Handle>();
  }

  std::shared_ptr<aws::utils::ScheduledCallback>
  schedule(Func f, std::chrono::milliseconds from_now) override {
    scheduled.push_back({std::move(f), from_now});
    return std::make_shared<Handle>();
  }

  size_t num_threads() const noexcept override { return 1; }
  void join() override {}

  // Runs the oldest pending scheduled task. Returns false if none pending.
  bool run_next_scheduled() {
    if (scheduled.empty()) {
      return false;
    }
    auto task = std::move(scheduled.front());
    scheduled.pop_front();
    task.func();
    return true;
  }

  std::deque<Scheduled> scheduled;
};

// Resolver returning scripted outcomes; counts calls per stream.
class RecordingResolver {
 public:
  explicit RecordingResolver(std::deque<boost::optional<StreamStrategy>> script)
      : script_(std::move(script)) {}

  boost::optional<StreamStrategy> operator()(const std::string& stream) {
    calls++;
    if (script_.empty()) {
      return boost::none;
    }
    auto next = script_.front();
    script_.pop_front();
    return next;
  }

  int calls = 0;

 private:
  std::deque<boost::optional<StreamStrategy>> script_;
};

struct ChangeRecorder {
  std::vector<std::pair<std::string, StreamStrategy>> changes;
  void operator()(const std::string& stream, StreamStrategy s) {
    changes.push_back({stream, s});
  }
};

// Test timing with negligible blocking backoff so retry tests run fast.
StreamStrategyManager::Timing fast_timing() {
  StreamStrategyManager::Timing t;
  t.blocking_backoff = std::chrono::milliseconds(1);
  return t;
}

} // namespace

BOOST_AUTO_TEST_SUITE(StreamStrategyManagerSuite)

BOOST_AUTO_TEST_CASE(StrategyStringRoundTrip) {
  using aws::kinesis::core::strategy_from_string;
  using aws::kinesis::core::strategy_to_string;

  BOOST_CHECK(strategy_from_string("AUTO") == StreamStrategy::AUTO);
  BOOST_CHECK(strategy_from_string("USER_PARTITION_KEY") ==
              StreamStrategy::USER_PARTITION_KEY);
  BOOST_CHECK(strategy_from_string("") == StreamStrategy::UNKNOWN);
  BOOST_CHECK(strategy_from_string("garbage") == StreamStrategy::UNKNOWN);

  BOOST_CHECK_EQUAL(strategy_to_string(StreamStrategy::AUTO), "AUTO");
  BOOST_CHECK_EQUAL(strategy_to_string(StreamStrategy::USER_PARTITION_KEY),
                    "USER_PARTITION_KEY");
  BOOST_CHECK_EQUAL(strategy_to_string(StreamStrategy::UNKNOWN), "");
  // The LEGACY_AGGREGATE fallback has no wire form (never sent to Java/service).
  BOOST_CHECK_EQUAL(strategy_to_string(StreamStrategy::LEGACY_AGGREGATE), "");
}

// Default = AUTO: get_or_discover returns AUTO immediately without a blocking
// resolver call.
BOOST_AUTO_TEST_CASE(DefaultAuto_NoBlock) {
  auto executor = std::make_shared<FakeExecutor>();
  auto resolver = std::make_shared<RecordingResolver>(
      std::deque<boost::optional<StreamStrategy>>{});
  ChangeRecorder changes;

  StreamStrategyManager mgr(
      executor, StreamStrategy::AUTO,
      [resolver](const std::string& s) { return (*resolver)(s); },
      [&changes](const std::string& s, StreamStrategy st) { changes(s, st); });

  BOOST_CHECK(mgr.get_or_discover("s") == StreamStrategy::AUTO);
  BOOST_CHECK_EQUAL(resolver->calls, 0);  // no blocking call
  BOOST_CHECK(mgr.get_strategy("s") == StreamStrategy::AUTO);
}

// Default = USER_PARTITION_KEY: returns UPK immediately.
BOOST_AUTO_TEST_CASE(DefaultUserPK_NoBlock) {
  auto executor = std::make_shared<FakeExecutor>();
  auto resolver = std::make_shared<RecordingResolver>(
      std::deque<boost::optional<StreamStrategy>>{});

  StreamStrategyManager mgr(
      executor, StreamStrategy::USER_PARTITION_KEY,
      [resolver](const std::string& s) { return (*resolver)(s); },
      [](const std::string&, StreamStrategy) {});

  BOOST_CHECK(mgr.get_or_discover("s") == StreamStrategy::USER_PARTITION_KEY);
  BOOST_CHECK_EQUAL(resolver->calls, 0);
}

// No default: first write resolves AUTO and fires the change callback.
BOOST_AUTO_TEST_CASE(NoDefault_DiscoversAuto) {
  auto executor = std::make_shared<FakeExecutor>();
  auto resolver = std::make_shared<RecordingResolver>(
      std::deque<boost::optional<StreamStrategy>>{StreamStrategy::AUTO});
  ChangeRecorder changes;

  StreamStrategyManager mgr(
      executor, StreamStrategy::UNKNOWN,
      [resolver](const std::string& s) { return (*resolver)(s); },
      [&changes](const std::string& s, StreamStrategy st) { changes(s, st); },
      fast_timing());

  BOOST_CHECK(mgr.get_or_discover("s") == StreamStrategy::AUTO);
  BOOST_CHECK_EQUAL(resolver->calls, 1);
  BOOST_REQUIRE_EQUAL(changes.changes.size(), 1u);
  BOOST_CHECK_EQUAL(changes.changes[0].first, "s");
  BOOST_CHECK(changes.changes[0].second == StreamStrategy::AUTO);
}

// No default: a transient failure is retried within the blocking budget.
BOOST_AUTO_TEST_CASE(NoDefault_RetriesThenSucceeds) {
  auto executor = std::make_shared<FakeExecutor>();
  auto resolver = std::make_shared<RecordingResolver>(
      std::deque<boost::optional<StreamStrategy>>{
          boost::none, StreamStrategy::USER_PARTITION_KEY});

  StreamStrategyManager mgr(
      executor, StreamStrategy::UNKNOWN,
      [resolver](const std::string& s) { return (*resolver)(s); },
      [](const std::string&, StreamStrategy) {},
      fast_timing());

  BOOST_CHECK(mgr.get_or_discover("s") == StreamStrategy::USER_PARTITION_KEY);
  BOOST_CHECK_EQUAL(resolver->calls, 2);  // failed once, then succeeded
}

// No default: all blocking attempts fail; the stream falls back to
// LEGACY_AGGREGATE (backward compat: keep aggregating) and a recovery is
// scheduled. The fallback is NOT reported to Java (no change callback).
BOOST_AUTO_TEST_CASE(NoDefault_AllFail_FallsBackToAggregate) {
  auto executor = std::make_shared<FakeExecutor>();
  auto resolver = std::make_shared<RecordingResolver>(
      std::deque<boost::optional<StreamStrategy>>{});  // always none
  ChangeRecorder changes;

  StreamStrategyManager mgr(
      executor, StreamStrategy::UNKNOWN,
      [resolver](const std::string& s) { return (*resolver)(s); },
      [&changes](const std::string& s, StreamStrategy st) { changes(s, st); },
      fast_timing());

  BOOST_CHECK(mgr.get_or_discover("s") == StreamStrategy::LEGACY_AGGREGATE);
  BOOST_CHECK(mgr.get_strategy("s") == StreamStrategy::LEGACY_AGGREGATE);
  BOOST_CHECK_EQUAL(resolver->calls, 3);       // blocking_max_attempts
  BOOST_CHECK(!executor->scheduled.empty());   // recovery scheduled
  BOOST_CHECK(changes.changes.empty());        // fallback not reported to Java
}

// No default + discovery keeps failing: after falling back to LEGACY_AGGREGATE,
// recovery keeps retrying on the recovery backoff. It must NOT prematurely drop
// to the (much longer) steady-state refresh just because a fallback strategy is
// now set -- LEGACY_AGGREGATE still counts as "not discovered".
BOOST_AUTO_TEST_CASE(NoDefault_FallbackRecoveryKeepsRetrying) {
  auto executor = std::make_shared<FakeExecutor>();
  auto resolver = std::make_shared<RecordingResolver>(
      std::deque<boost::optional<StreamStrategy>>{});  // always none

  StreamStrategyManager mgr(
      executor, StreamStrategy::UNKNOWN,
      [resolver](const std::string& s) { return (*resolver)(s); },
      [](const std::string&, StreamStrategy) {},
      fast_timing());

  BOOST_CHECK(mgr.get_or_discover("s") == StreamStrategy::LEGACY_AGGREGATE);
  // First recovery scheduled at recovery_backoff[0] (5s).
  BOOST_REQUIRE_EQUAL(executor->scheduled.size(), 1u);
  BOOST_CHECK(executor->scheduled.front().delay ==
              std::chrono::milliseconds(5000));

  // Run it; still fails, so it schedules the next recovery at 15s (not the 300s
  // steady refresh), and the stream stays on the fallback.
  executor->run_next_scheduled();
  BOOST_REQUIRE_EQUAL(executor->scheduled.size(), 1u);
  BOOST_CHECK(executor->scheduled.front().delay ==
              std::chrono::milliseconds(15000));
  BOOST_CHECK(mgr.get_strategy("s") == StreamStrategy::LEGACY_AGGREGATE);
}

// No default: blocking discovery fails, the stream falls back to
// LEGACY_AGGREGATE, and background recovery later resolves the real strategy
// (USER_PARTITION_KEY). The switch to a confirmed strategy fires the change
// callback (Java is now told), unlike the silent fallback.
BOOST_AUTO_TEST_CASE(NoDefault_FallbackThenRecoversToUserPK) {
  auto executor = std::make_shared<FakeExecutor>();
  // Three blocking failures, then recovery resolves USER_PARTITION_KEY.
  auto resolver = std::make_shared<RecordingResolver>(
      std::deque<boost::optional<StreamStrategy>>{
          boost::none, boost::none, boost::none,
          StreamStrategy::USER_PARTITION_KEY});
  ChangeRecorder changes;

  StreamStrategyManager mgr(
      executor, StreamStrategy::UNKNOWN,
      [resolver](const std::string& s) { return (*resolver)(s); },
      [&changes](const std::string& s, StreamStrategy st) { changes(s, st); },
      fast_timing());

  BOOST_CHECK(mgr.get_or_discover("s") == StreamStrategy::LEGACY_AGGREGATE);
  BOOST_CHECK(changes.changes.empty());  // fallback is silent

  // Run the scheduled recovery: it resolves UPK and reports the change.
  BOOST_REQUIRE(executor->run_next_scheduled());
  BOOST_CHECK(mgr.get_strategy("s") == StreamStrategy::USER_PARTITION_KEY);
  BOOST_REQUIRE_EQUAL(changes.changes.size(), 1u);
  BOOST_CHECK(changes.changes[0].second == StreamStrategy::USER_PARTITION_KEY);
}

// Same, but recovery resolves AUTO: the stream switches off aggregation (from
// the LEGACY_AGGREGATE fallback to AUTO) once discovery succeeds.
BOOST_AUTO_TEST_CASE(NoDefault_FallbackThenRecoversToAuto) {
  auto executor = std::make_shared<FakeExecutor>();
  auto resolver = std::make_shared<RecordingResolver>(
      std::deque<boost::optional<StreamStrategy>>{
          boost::none, boost::none, boost::none, StreamStrategy::AUTO});
  ChangeRecorder changes;

  StreamStrategyManager mgr(
      executor, StreamStrategy::UNKNOWN,
      [resolver](const std::string& s) { return (*resolver)(s); },
      [&changes](const std::string& s, StreamStrategy st) { changes(s, st); },
      fast_timing());

  BOOST_CHECK(mgr.get_or_discover("s") == StreamStrategy::LEGACY_AGGREGATE);

  BOOST_REQUIRE(executor->run_next_scheduled());
  BOOST_CHECK(mgr.get_strategy("s") == StreamStrategy::AUTO);
  BOOST_REQUIRE_EQUAL(changes.changes.size(), 1u);
  BOOST_CHECK(changes.changes[0].second == StreamStrategy::AUTO);
}

// A second first-write while discovery is done does not re-run the blocking
// resolver.
BOOST_AUTO_TEST_CASE(NoDefault_SecondCallDoesNotRediscover) {
  auto executor = std::make_shared<FakeExecutor>();
  auto resolver = std::make_shared<RecordingResolver>(
      std::deque<boost::optional<StreamStrategy>>{StreamStrategy::AUTO});

  StreamStrategyManager mgr(
      executor, StreamStrategy::UNKNOWN,
      [resolver](const std::string& s) { return (*resolver)(s); },
      [](const std::string&, StreamStrategy) {},
      fast_timing());

  BOOST_CHECK(mgr.get_or_discover("s") == StreamStrategy::AUTO);
  BOOST_CHECK(mgr.get_or_discover("s") == StreamStrategy::AUTO);
  BOOST_CHECK_EQUAL(resolver->calls, 1);
}

// Wrong Shard: triggers a re-check only after the threshold of consecutive hits.
BOOST_AUTO_TEST_CASE(WrongShard_TriggersAtThreshold) {
  auto executor = std::make_shared<FakeExecutor>();
  // First the (default) discovery resolves UPK, then the wrong-shard re-check
  // resolves AUTO.
  auto resolver = std::make_shared<RecordingResolver>(
      std::deque<boost::optional<StreamStrategy>>{StreamStrategy::AUTO});
  ChangeRecorder changes;

  StreamStrategyManager mgr(
      executor, StreamStrategy::UNKNOWN,
      [resolver](const std::string& s) { return (*resolver)(s); },
      [&changes](const std::string& s, StreamStrategy st) { changes(s, st); },
      fast_timing());

  // Below threshold: no resolver call (submit runs inline, so we'd see it).
  mgr.record_wrong_shard("s");
  mgr.record_wrong_shard("s");
  BOOST_CHECK_EQUAL(resolver->calls, 0);

  // Third consecutive hit triggers an immediate re-check.
  mgr.record_wrong_shard("s");
  BOOST_CHECK_EQUAL(resolver->calls, 1);
  BOOST_CHECK(mgr.get_strategy("s") == StreamStrategy::AUTO);
}

// Wrong Shard: reaching the threshold triggers a re-check, which resets the
// counter. So a second re-check only fires after another full threshold of
// wrong-shard hits, not on the very next one.
BOOST_AUTO_TEST_CASE(WrongShard_CounterResetsAfterRecheck) {
  auto executor = std::make_shared<FakeExecutor>();
  // Two re-checks available; both resolve a strategy (which resets the counter).
  auto resolver = std::make_shared<RecordingResolver>(
      std::deque<boost::optional<StreamStrategy>>{
          StreamStrategy::AUTO, StreamStrategy::AUTO});

  StreamStrategyManager mgr(
      executor, StreamStrategy::UNKNOWN,
      [resolver](const std::string& s) { return (*resolver)(s); },
      [](const std::string&, StreamStrategy) {},
      fast_timing());

  // First three hits -> one re-check, counter resets.
  mgr.record_wrong_shard("s");
  mgr.record_wrong_shard("s");
  mgr.record_wrong_shard("s");
  BOOST_CHECK_EQUAL(resolver->calls, 1);

  // Two more hits are below threshold again (counter was reset).
  mgr.record_wrong_shard("s");
  mgr.record_wrong_shard("s");
  BOOST_CHECK_EQUAL(resolver->calls, 1);

  // The third hit since the reset triggers the second re-check.
  mgr.record_wrong_shard("s");
  BOOST_CHECK_EQUAL(resolver->calls, 2);
}

// Wrong Shard re-check is debounced: while a re-check is in flight for a stream,
// further bursts of wrong-shard hits do NOT submit additional re-checks (which
// would each fire a redundant DescribeStreamSummary). Simulated by having the
// resolver re-enter record_wrong_shard mid-call (i.e. wrong shards arriving
// while the DSS call is outstanding); with the guard, only the first re-check's
// resolver call happens.
BOOST_AUTO_TEST_CASE(WrongShard_RecheckDebounced) {
  auto executor = std::make_shared<FakeExecutor>();

  // The resolver, on its first invocation, simulates a storm of wrong-shard
  // retries arriving while this very re-check (DSS call) is still in flight.
  // Without the in-flight guard each of these would trip the threshold and
  // submit another re-check -> more resolver calls. With the guard they are
  // suppressed, so the resolver is called exactly once.
  StreamStrategyManager* mgr_ptr = nullptr;
  int resolver_calls = 0;
  auto resolver = [&](const std::string& s) -> boost::optional<StreamStrategy> {
    if (++resolver_calls == 1) {
      // Many more wrong shards than the threshold, all while in flight.
      for (int i = 0; i < 30; i++) {
        mgr_ptr->record_wrong_shard(s);
      }
    }
    return StreamStrategy::AUTO;
  };

  StreamStrategyManager mgr(
      executor, StreamStrategy::UNKNOWN, resolver,
      [](const std::string&, StreamStrategy) {}, fast_timing());
  mgr_ptr = &mgr;

  // Trip the threshold once -> one re-check submitted -> resolver runs (inline),
  // and during that run 30 more wrong shards arrive but must be suppressed.
  mgr.record_wrong_shard("s");
  mgr.record_wrong_shard("s");
  mgr.record_wrong_shard("s");

  // Exactly one resolver call despite the 30 in-flight wrong-shard hits.
  BOOST_CHECK_EQUAL(resolver_calls, 1);
  BOOST_CHECK(mgr.get_strategy("s") == StreamStrategy::AUTO);

  // After the re-check completed the guard is cleared, so a fresh threshold of
  // hits can trigger another re-check.
  mgr.record_wrong_shard("s");
  mgr.record_wrong_shard("s");
  mgr.record_wrong_shard("s");
  BOOST_CHECK_EQUAL(resolver_calls, 2);
}

// A scheduled refresh that observes a changed strategy fires the change
// callback.
BOOST_AUTO_TEST_CASE(Refresh_DetectsChange) {
  auto executor = std::make_shared<FakeExecutor>();
  // Discovery resolves AUTO; a later refresh resolves USER_PARTITION_KEY.
  auto resolver = std::make_shared<RecordingResolver>(
      std::deque<boost::optional<StreamStrategy>>{
          StreamStrategy::AUTO, StreamStrategy::USER_PARTITION_KEY});
  ChangeRecorder changes;

  StreamStrategyManager mgr(
      executor, StreamStrategy::UNKNOWN,
      [resolver](const std::string& s) { return (*resolver)(s); },
      [&changes](const std::string& s, StreamStrategy st) { changes(s, st); },
      fast_timing());

  BOOST_CHECK(mgr.get_or_discover("s") == StreamStrategy::AUTO);
  BOOST_REQUIRE_EQUAL(changes.changes.size(), 1u);

  mgr.refresh_one("s");
  BOOST_CHECK(mgr.get_strategy("s") == StreamStrategy::USER_PARTITION_KEY);
  BOOST_REQUIRE_EQUAL(changes.changes.size(), 2u);
  BOOST_CHECK(changes.changes[1].second == StreamStrategy::USER_PARTITION_KEY);
}

BOOST_AUTO_TEST_SUITE_END()
