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

#include <aws/kinesis/core/aggregator.h>
#include <aws/kinesis/core/test/test_utils.h>
#include <aws/utils/io_service_executor.h>
#include <aws/utils/utils.h>
#include <aws/utils/processing_statistics_logger.h>

namespace {

using FlushCallback =
    std::function<void (std::shared_ptr<aws::kinesis::core::KinesisRecord>)>;

const size_t kCountLimit = 100;

std::string get_hash_key(uint64_t shard_id) {
  std::unordered_map<uint64_t, std::string> shard_id_to_hash_key;
  shard_id_to_hash_key.emplace(1, "170141183460469231731687303715884105728");
  shard_id_to_hash_key.emplace(2, "0");
  shard_id_to_hash_key.emplace(3, "85070591730234615865843651857942052863");
  return shard_id_to_hash_key[shard_id];
}

class MockShardMap : public aws::kinesis::core::ShardMap {
 public:
  MockShardMap(bool down = false) : down_(down) {
    limits_.emplace_back("85070591730234615865843651857942052862");
    shard_ids_.emplace_back(2);

    limits_.emplace_back("170141183460469231731687303715884105727");
    shard_ids_.emplace_back(3);

    limits_.emplace_back("340282366920938463463374607431768211455");
    shard_ids_.emplace_back(1);
  }

  boost::optional<uint64_t> shard_id(
      const boost::multiprecision::uint128_t& hash_key) {
    if (down_) {
      return boost::none;
    }

    for (size_t i = 0; i < shard_ids_.size(); i++) {
      if (hash_key <= limits_[i]) {
        return shard_ids_[i];
      }
    }

    return boost::none;
  }

 private:
  bool down_;
  std::vector<uint64_t> shard_ids_;
  std::vector<boost::multiprecision::uint128_t> limits_;
};

  aws::utils::flush_statistics_aggregator flush_stats("Test", "InRecords", "OutRecords");

auto make_aggregator(
    bool shard_map_down = false,
    FlushCallback cb = [](auto) {},
    std::shared_ptr<aws::kinesis::core::Configuration> config =
        std::shared_ptr<aws::kinesis::core::Configuration>()) {
  if (!config) {
    config = std::make_shared<aws::kinesis::core::Configuration>();
    config->aggregation_max_count(kCountLimit);
  }

  return std::make_shared<aws::kinesis::core::Aggregator>(
          std::make_shared<aws::utils::IoServiceExecutor>(4),
          std::make_shared<MockShardMap>(shard_map_down),
          cb,
          config,
          flush_stats);
}

} //namespace

BOOST_AUTO_TEST_SUITE(Aggregator)

BOOST_AUTO_TEST_CASE(Basic) {
  auto aggregator = make_aggregator();

  for (uint64_t shard_id = 1; shard_id <= 3; shard_id++) {
    aws::kinesis::test::UserRecordSharedPtrVector v;
    std::shared_ptr<aws::kinesis::core::KinesisRecord> kr;

    for (size_t i = 0; i < kCountLimit; i++) {
      auto ur =
          aws::kinesis::test::make_user_record(
              "pk",
              std::to_string(::rand()),
              get_hash_key(shard_id),
              10000 + i);
      kr = aggregator->put(ur);
      v.push_back(ur);

      BOOST_REQUIRE(ur->predicted_shard());
      BOOST_CHECK_EQUAL(*ur->predicted_shard(), shard_id);
    }

    BOOST_REQUIRE(kr);
    aws::kinesis::test::verify(v, *kr);
  }
}

BOOST_AUTO_TEST_CASE(ShardMapDown) {
  auto aggregator = make_aggregator(true);

  for (int i = 0; i < 100; i++) {
    auto ur =
      aws::kinesis::test::make_user_record(
          "pk",
          std::to_string(::rand()),
          std::to_string(::rand()));
    auto kr = aggregator->put(ur);
    BOOST_REQUIRE(kr);
    aws::kinesis::test::verify_unaggregated(ur, *kr);
  }
}

BOOST_AUTO_TEST_CASE(AggregationDisabled) {
  auto config = std::make_shared<aws::kinesis::core::Configuration>();
  config->aggregation_enabled(false);
  auto aggregator = make_aggregator(false, [](auto){}, config);

  for (int i = 0; i < 100; i++) {
    auto ur =
        aws::kinesis::test::make_user_record(
            "pk",
            std::to_string(::rand()),
            std::to_string(::rand()));
    auto kr = aggregator->put(ur);
    BOOST_REQUIRE(kr);
    aws::kinesis::test::verify_unaggregated(ur, *kr);
  }
}

BOOST_AUTO_TEST_CASE(Concurrency) {

  std::atomic<size_t> count(0);
  auto aggregator = make_aggregator(
      false,
      [&](auto kr) {
        count += kr->size();
      });

  const size_t N = 250;

  for (size_t num_threads : { 2, 8, 16 }) {
    count.store(0);

    auto go_time =
      std::chrono::high_resolution_clock::now() + std::chrono::milliseconds(50);

    std::vector<aws::thread> threads;
    for (size_t i = 0; i < num_threads; i++) {
      threads.emplace_back([&] {
        aws::utils::sleep_until(go_time);
        for (size_t j = 0; j < N * kCountLimit; j++) {
          auto ur =
              aws::kinesis::test::make_user_record(
                  "pk",
                  std::to_string(::rand()),
                  "0");
          auto kr = aggregator->put(ur);
          if (kr) {
            count += kr->size();
          }
        }
      });
    }

    for (auto& t : threads) {
      t.join();
    }
    aggregator->flush();

    const size_t total_records = num_threads * N * kCountLimit;
    double seconds = aws::utils::seconds_since(go_time);
    LOG(info) << "Aggregator throughput (" << num_threads
              << " threads contending on a single shard): "
              << total_records / seconds / 1000 << " Krps";

    BOOST_CHECK_EQUAL(count, total_records);
  }
}

BOOST_AUTO_TEST_SUITE_END()
