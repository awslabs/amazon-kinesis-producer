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

#include <condition_variable>
#include <mutex>

#include <boost/test/unit_test.hpp>

#include <aws/kinesis/core/aggregator.h>
#include <aws/kinesis/core/test/test_utils.h>
#include <aws/utils/io_service_executor.h>
#include <aws/http/io_service_socket.h>
#include <aws/utils/utils.h>

namespace {

using FlushCallback =
    std::function<void (std::shared_ptr<aws::kinesis::core::KinesisRecord>)>;

const int kPort = aws::kinesis::test::TestTLSServer::kDefaultPort;

const size_t kCountLimit = 100;

std::string get_hash_key(uint64_t shard_id) {
  std::unordered_map<uint64_t, std::string> shard_id_to_hash_key;
  shard_id_to_hash_key.emplace(1, "170141183460469231731687303715884105728");
  shard_id_to_hash_key.emplace(2, "0");
  shard_id_to_hash_key.emplace(3, "85070591730234615865843651857942052863");
  return shard_id_to_hash_key[shard_id];
}

auto describe_stream_handler = [](auto& req) {
  aws::http::HttpResponse res(200);
  res.set_data(R"XXXX(
  {
    "StreamDescription": {
      "StreamStatus": "ACTIVE",
      "StreamName": "test",
      "StreamARN": "arn:aws:kinesis:us-west-2:263868185958:stream\/test",
      "Shards": [
        {
          "HashKeyRange": {
            "EndingHashKey": "340282366920938463463374607431768211455",
            "StartingHashKey": "170141183460469231731687303715884105728"
          },
          "ShardId": "shardId-000000000001",
          "SequenceNumberRange": {
            "StartingSequenceNumber": "49549167410945534708633744510750617797212193316405248018"
          }
        },
        {
          "HashKeyRange": {
            "EndingHashKey": "85070591730234615865843651857942052862",
            "StartingHashKey": "0"
          },
          "ShardId": "shardId-000000000002",
          "ParentShardId": "shardId-000000000000",
          "SequenceNumberRange": {
            "StartingSequenceNumber": "49549169978943246555030591128013184047489460388642160674"
          }
        },
        {
          "HashKeyRange": {
            "EndingHashKey": "170141183460469231731687303715884105727",
            "StartingHashKey": "85070591730234615865843651857942052863"
          },
          "ShardId": "shardId-000000000003",
          "ParentShardId": "shardId-000000000000",
          "SequenceNumberRange": {
            "StartingSequenceNumber": "49549169978965547300229121751154719765762108750148141106"
          }
        }
      ]
    }
  }
  )XXXX");
  return res;
};

auto make_aggregator(
    FlushCallback cb = [](auto) {},
    std::shared_ptr<aws::kinesis::core::Configuration> config =
        std::shared_ptr<aws::kinesis::core::Configuration>()) {
  auto executor = std::make_shared<aws::utils::IoServiceExecutor>(4);
  auto factory = std::make_shared<aws::http::IoServiceSocketFactory>();

  auto http_client =
      std::make_shared<aws::http::HttpClient>(
          executor,
          factory,
          "localhost",
          kPort,
          true,
          false);

  auto creds =
      std::make_shared<aws::auth::BasicAwsCredentialsProvider>(
          "AKIAAAAAAAAAAAAAAAAA",
          "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");

  auto shard_map =
      std::make_shared<aws::kinesis::core::ShardMap>(
          executor,
          http_client,
          creds,
          "us-west-1",
          "test",
          std::make_shared<aws::metrics::NullMetricsManager>(),
          std::chrono::milliseconds(100),
          std::chrono::milliseconds(1000));

  if (!config) {
    config = std::make_shared<aws::kinesis::core::Configuration>();
    config->aggregation_max_count(kCountLimit);
  }

  auto aggregator =
      std::make_shared<aws::kinesis::core::Aggregator>(
          executor,
          shard_map,
          cb,
          config);

  return aggregator;
}

} //namespace

BOOST_AUTO_TEST_SUITE(Aggregator)

BOOST_AUTO_TEST_CASE(Basic) {
  aws::kinesis::test::TestTLSServer server;
  server.enqueue_handler(describe_stream_handler);

  auto aggregator = make_aggregator();

  // allow shard map to update
  aws::utils::sleep_for(std::chrono::milliseconds(1500));

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
  auto aggregator = make_aggregator();

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
  aws::kinesis::test::TestTLSServer server;
  server.enqueue_handler(describe_stream_handler);

  auto config = std::make_shared<aws::kinesis::core::Configuration>();
  config->aggregation_enabled(false);
  auto aggregator = make_aggregator([](auto){}, config);

  // allow shard map to update
  aws::utils::sleep_for(std::chrono::milliseconds(1500));

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
  aws::kinesis::test::TestTLSServer server;
  server.enqueue_handler(describe_stream_handler);

  std::atomic<size_t> count(0);
  auto aggregator = make_aggregator([&](auto kr) {
    count += kr->size();
  });

  // allow shard map to update
  aws::utils::sleep_for(std::chrono::milliseconds(1500));

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
