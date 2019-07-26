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

#include <aws/core/utils/json/JsonSerializer.h>
#include <aws/kinesis/core/retrier.h>
#include <aws/kinesis/core/test/test_utils.h>
#include <aws/utils/logging.h>

namespace {

using TimePoint = std::chrono::steady_clock::time_point;

auto success_outcome(std::string json) {
  Aws::Utils::Json::JsonValue j(json);
  Aws::Http::HeaderValueCollection h;
  Aws::AmazonWebServiceResult<Aws::Utils::Json::JsonValue> awsr(j, h);
  Aws::Kinesis::Model::PutRecordsResult r(awsr);
  Aws::Kinesis::Model::PutRecordsOutcome o(r);
  return o;
}

Aws::Kinesis::Model::PutRecordsOutcome
error_outcome(std::string name, std::string msg) {
  return Aws::Kinesis::Model::PutRecordsOutcome(
      Aws::Client::AWSError<Aws::Kinesis::KinesisErrors>(
          Aws::Kinesis::KinesisErrors::UNKNOWN,
          name,
          msg,
          false));
}

auto make_prr_ctx(size_t num_kr,
                  size_t num_ur_per_kr,
                  Aws::Kinesis::Model::PutRecordsOutcome outcome) {
  std::vector<std::shared_ptr<aws::kinesis::core::KinesisRecord>> krs;
  for (size_t i = 0; i < num_kr; i++) {
    auto kr = std::make_shared<aws::kinesis::core::KinesisRecord>();
    for (size_t j = 0; j < num_ur_per_kr; j++) {
      auto ur = aws::kinesis::test::make_user_record();
      ur->predicted_shard(i);
      kr->add(ur);
    }
    krs.push_back(kr);
  }
  auto ctx = std::make_shared<aws::kinesis::core::PutRecordsContext>(
      "myStream",
      krs);
  ctx->set_outcome(outcome);
  return ctx;
}

} //namespace

BOOST_AUTO_TEST_SUITE(Retrier)

// Case where there are no errors
BOOST_AUTO_TEST_CASE(Success) {
  auto num_ur_per_kr = 10;
  auto num_kr = 2;

  auto ctx = make_prr_ctx(
      num_kr,
      num_ur_per_kr,
      success_outcome(R"(
      {
        "FailedRecordCount": 0,
        "Records":[
          {
            "SequenceNumber":"1234",
            "ShardId":"shardId-000000000000"
          },
          {
            "SequenceNumber":"4567",
            "ShardId":"shardId-000000000001"
          }
        ]
      }
      )"));

  auto start = std::chrono::steady_clock::now();
  auto end = start + std::chrono::milliseconds(5);
  ctx->set_start(start);
  ctx->set_end(end);

  size_t count = 0;
  aws::kinesis::core::Retrier retrier(
      std::make_shared<aws::kinesis::core::Configuration>(),
      [&](auto& ur) {
        auto& attempts = ur->attempts();
        BOOST_CHECK_EQUAL(attempts.size(), 1);
        BOOST_CHECK(attempts[0].start() == start);
        BOOST_CHECK(attempts[0].end() == start + std::chrono::milliseconds(5));
        BOOST_CHECK((bool) attempts[0]);

        if (count++ / num_ur_per_kr == 0) {
          BOOST_CHECK_EQUAL(attempts[0].sequence_number(), "1234");
          BOOST_CHECK_EQUAL(attempts[0].shard_id(), "shardId-000000000000");
        } else {
          BOOST_CHECK_EQUAL(attempts[0].sequence_number(), "4567");
          BOOST_CHECK_EQUAL(attempts[0].shard_id(), "shardId-000000000001");
        }
      },
      [&](auto& ur) {
        BOOST_FAIL("Retry should not be called");
      },
      [&](auto) {
        BOOST_FAIL("Shard map invalidate should not be called");
      });

  retrier.put(ctx);

  BOOST_CHECK_EQUAL(count, num_kr * num_ur_per_kr);
}

BOOST_AUTO_TEST_CASE(RequestFailure) {
  auto ctx = make_prr_ctx(1, 10, error_outcome("code", "msg"));

  auto start = std::chrono::steady_clock::now();
  auto end = start + std::chrono::milliseconds(5);
  ctx->set_start(start);
  ctx->set_end(end);

  size_t count = 0;
  aws::kinesis::core::Retrier retrier(
      std::make_shared<aws::kinesis::core::Configuration>(),
      [&](auto& ur) {
        BOOST_FAIL("Finish should not be called");
      },
      [&](auto& ur) {
        count++;
        auto& attempts = ur->attempts();
        BOOST_CHECK_EQUAL(attempts.size(), 1);
        BOOST_CHECK(attempts[0].start() == start);
        BOOST_CHECK(attempts[0].end() == start + std::chrono::milliseconds(5));
        BOOST_CHECK(!(bool) attempts[0]);
        BOOST_CHECK_EQUAL(attempts[0].error_code(), "code");
        BOOST_CHECK_EQUAL(attempts[0].error_message(), "msg");
      },
      [&](auto) {
        BOOST_FAIL("Shard map invalidate should not be called");
      });

  retrier.put(ctx);

  BOOST_CHECK_EQUAL(count, 10);
}


// A mix of success and failures in a PutRecordsResult.
BOOST_AUTO_TEST_CASE(Partial) {
  auto num_ur_per_kr = 10;
  auto num_kr = 6;

  auto ctx = make_prr_ctx(
      num_kr,
      num_ur_per_kr,
      success_outcome(R"(
      {
        "FailedRecordCount": 4,
        "Records":[
          {
            "SequenceNumber":"1234",
            "ShardId":"shardId-000000000000"
          },
          {
            "SequenceNumber":"4567",
            "ShardId":"shardId-000000000001"
          },
          {
            "ErrorCode":"xx",
            "ErrorMessage":"yy"
          },
          {
            "ErrorCode":"InternalFailure",
            "ErrorMessage":"Internal service failure."
          },
          {
            "ErrorCode":"ServiceUnavailable",
            "ErrorMessage":""
          },
          {
            "ErrorCode":"ProvisionedThroughputExceededException",
            "ErrorMessage":"..."
          }
        ]
      }
      )"));

  size_t count = 0;
  aws::kinesis::core::Retrier retrier(
      std::make_shared<aws::kinesis::core::Configuration>(),
      [&](auto& ur) {
        auto& attempts = ur->attempts();
        BOOST_CHECK_EQUAL(attempts.size(), 1);

        auto record_group = count++ / num_ur_per_kr;

        if (record_group == 0) {
          BOOST_CHECK((bool) attempts[0]);
          BOOST_CHECK_EQUAL(attempts[0].sequence_number(), "1234");
          BOOST_CHECK_EQUAL(attempts[0].shard_id(), "shardId-000000000000");
        } else if (record_group == 1)  {
          BOOST_CHECK((bool) attempts[0]);
          BOOST_CHECK_EQUAL(attempts[0].sequence_number(), "4567");
          BOOST_CHECK_EQUAL(attempts[0].shard_id(), "shardId-000000000001");
        } else if (record_group == 2) {
          BOOST_CHECK(!(bool) attempts[0]);
          BOOST_CHECK_EQUAL(attempts[0].error_code(), "xx");
          BOOST_CHECK_EQUAL(attempts[0].error_message(), "yy");
        }
      },
      [&](auto& ur) {
        auto& attempts = ur->attempts();
        BOOST_CHECK_EQUAL(attempts.size(), 1);
        BOOST_CHECK(!(bool) attempts[0]);

        auto record_group = count++ / num_ur_per_kr;

        if (record_group == 3) {
          BOOST_CHECK_EQUAL(attempts[0].error_code(), "InternalFailure");
          BOOST_CHECK_EQUAL(attempts[0].error_message(),
                            "Internal service failure.");
        } else if (record_group == 4)  {
          BOOST_CHECK_EQUAL(attempts[0].error_code(),
                            "ServiceUnavailable");
          BOOST_CHECK_EQUAL(attempts[0].error_message(), "");
        } else if (record_group == 5)  {
          BOOST_CHECK_EQUAL(attempts[0].error_code(),
                            "ProvisionedThroughputExceededException");
          BOOST_CHECK_EQUAL(attempts[0].error_message(), "...");
        }
      },
      [&](auto) {
        BOOST_FAIL("Shard map invalidate should not be called");
      });

  retrier.put(ctx);

  BOOST_CHECK_EQUAL(count, num_kr * num_ur_per_kr);
}

BOOST_AUTO_TEST_CASE(FailIfThrottled) {
  auto ctx = make_prr_ctx(
      1,
      10,
      success_outcome(R"(
      {
        "FailedRecordCount": 1,
        "Records":[
          {
            "ErrorCode":"ProvisionedThroughputExceededException",
            "ErrorMessage":"..."
          }
        ]
      }
      )"));

  auto config = std::make_shared<aws::kinesis::core::Configuration>();
  config->fail_if_throttled(true);

  size_t count = 0;
  aws::kinesis::core::Retrier retrier(
      config,
      [&](auto& ur) {
        count++;
        auto& attempts = ur->attempts();
        BOOST_CHECK_EQUAL(attempts.size(), 1);
        BOOST_CHECK(!(bool) attempts[0]);
        BOOST_CHECK_EQUAL(attempts[0].error_code(),
                          "ProvisionedThroughputExceededException");
        BOOST_CHECK_EQUAL(attempts[0].error_message(), "...");
      },
      [&](auto& ur) {
        BOOST_FAIL("Retry should not be called");
      },
      [&](auto) {
        BOOST_FAIL("Shard map invalidate should not be called");
      });

  retrier.put(ctx);

  BOOST_CHECK_EQUAL(count, 10);
}

BOOST_AUTO_TEST_CASE(WrongShard) {
  auto ctx = make_prr_ctx(
      1,
      1,
      success_outcome(R"(
      {
        "FailedRecordCount": 0,
        "Records":[
          {
            "SequenceNumber":"1234",
            "ShardId":"shardId-000000000004"
          }
        ]
      }
      )"));

  size_t count = 0;
  bool shard_map_invalidated = false;

  aws::kinesis::core::Retrier retrier(
      std::make_shared<aws::kinesis::core::Configuration>(),
      [&](auto& ur) {
        BOOST_FAIL("Finish should not be called");
      },
      [&](auto& ur) {
        count++;
        auto& attempts = ur->attempts();
        BOOST_CHECK_EQUAL(attempts.size(), 1);
        BOOST_CHECK(!(bool) attempts[0]);
        BOOST_CHECK_EQUAL(attempts[0].error_code(), "Wrong Shard");
      },
      [&](auto) {
        shard_map_invalidated = true;
      });

  retrier.put(ctx);

  BOOST_CHECK_MESSAGE(shard_map_invalidated,
                      "Shard map should've been invalidated.");
  BOOST_CHECK_EQUAL(count, 1);
}

BOOST_AUTO_TEST_SUITE_END()
