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
                  Aws::Kinesis::Model::PutRecordsOutcome outcome,
                  const std::string& explicit_hash_key = "") {
  std::vector<std::shared_ptr<aws::kinesis::core::KinesisRecord>> krs;
  for (size_t i = 0; i < num_kr; i++) {
    auto kr = std::make_shared<aws::kinesis::core::KinesisRecord>();
    for (size_t j = 0; j < num_ur_per_kr; j++) {
      auto ur = aws::kinesis::test::make_user_record_with_hashkey(explicit_hash_key);
      ur->predicted_shard(i);
      kr->add(ur);
    }
    krs.push_back(kr);
  }
  auto ctx = std::make_shared<aws::kinesis::core::PutRecordsContext>(
      "myStream",
      "arn:aws:kinesis:us-east-2:123456789012:stream/myStream",
      "",  // stream_id (empty for test)
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
        return boost::none;
      },
      [&](auto, auto) {
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
        return boost::none;
      },
      [&](auto, auto) {
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
        return boost::none;
      },
      [&](auto, auto) {
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
        return boost::none;
      },
      [&](auto, auto) {
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
        return boost::none;
      },
      [&](auto, auto) {
        shard_map_invalidated = true;
      });

  retrier.put(ctx);

  BOOST_CHECK_MESSAGE(shard_map_invalidated,
                      "Shard map should've been invalidated.");
  BOOST_CHECK_EQUAL(count, 1);
}

// On a Wrong Shard, the retrier notifies the wrong-shard callback with the
// stream name (wired to strategy re-discovery in production).
BOOST_AUTO_TEST_CASE(WrongShard_NotifiesCallback) {
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

  std::vector<std::string> wrong_shard_streams;
  aws::kinesis::core::Retrier retrier(
      std::make_shared<aws::kinesis::core::Configuration>(),
      [&](auto& ur) {},                       // finish
      [&](auto& ur) {},                       // retry
      [&](auto) { return boost::none; },      // hashrange
      [&](auto, auto) {},                     // invalidate
      aws::kinesis::core::Retrier::ErrorCallback(),
      std::make_shared<aws::metrics::NullMetricsManager>(),
      [&](const std::string& stream) { wrong_shard_streams.push_back(stream); });

  retrier.put(ctx);

  BOOST_REQUIRE_EQUAL(wrong_shard_streams.size(), 1u);
  BOOST_CHECK_EQUAL(wrong_shard_streams[0], "myStream");
}


BOOST_AUTO_TEST_CASE(InvalidateForFirstUserRecordOnly) {
  auto ctx = make_prr_ctx(
      1,
      10,
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
  int num_shard_map_invalidated = 0;

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
        return boost::none;
      },
      [&](auto, auto) {
        num_shard_map_invalidated++;
      });

  retrier.put(ctx);

  BOOST_CHECK_EQUAL(num_shard_map_invalidated, 1);
  BOOST_CHECK_EQUAL(count, 10);

}

// This is not true anymore because of this https://github.com/awslabs/amazon-kinesis-producer/pull/318. This should be fixed
// BOOST_AUTO_TEST_CASE(InvalidateForPredictedShardIdsLowerThanActual) {
//   auto ctx = make_prr_ctx(
//       3,
//       10,
//       success_outcome(R"(
//       {
//         "FailedRecordCount": 0,
//         "Records":[
//           {
//             "SequenceNumber":"1234",
//             "ShardId":"shardId-000000000002"
//           },
//           {
//             "SequenceNumber":"1235",
//             "ShardId":"shardId-000000000002"
//           },
//           {
//             "SequenceNumber":"1236",
//             "ShardId":"shardId-000000000001"
//           }
//         ]
//       }
//       )"));

//   size_t count = 0;
//   int num_shard_map_invalidated = 0;

//   aws::kinesis::core::Retrier retrier(
//       std::make_shared<aws::kinesis::core::Configuration>(),
//       [&](auto& ur) {
//         BOOST_FAIL("Finish should not be called");
//       },
//       [&](auto& ur) {
//         count++;
//         auto& attempts = ur->attempts();
//         BOOST_CHECK_EQUAL(attempts.size(), 1);
//         BOOST_CHECK(!(bool) attempts[0]);
//         BOOST_CHECK_EQUAL(attempts[0].error_code(), "Wrong Shard");
//       },
//       [&](auto) {
//         return boost::none;
//       },
//       [&](auto, auto) {
//         num_shard_map_invalidated++;
//       });

//   retrier.put(ctx);

//   BOOST_CHECK_EQUAL(num_shard_map_invalidated, 2);
//   BOOST_CHECK_EQUAL(count, 30);

// }

BOOST_AUTO_TEST_CASE(WrongShardButCorrectHashrange) {
  // The actual shard will cover hashrange 0 to 10. So the below hash key should pass. 
  std::vector<std::string> arr = {"0", "1", "5", "10"};
  for (const std::string& hashkey : arr) {
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
        )"),
        hashkey);

    bool shard_map_invalidated = false;

    aws::kinesis::core::Retrier retrier(
        std::make_shared<aws::kinesis::core::Configuration>(),
        [&](auto& ur) {
          auto& attempts = ur->attempts();
          BOOST_CHECK_EQUAL(attempts.size(), 1);
        },
        [&](auto& ur) {
          BOOST_FAIL("Retry should not be called");
        },
        [&](auto) {
          return std::make_pair(boost::multiprecision::uint128_t(0), boost::multiprecision::uint128_t(10));
        },
        [&](auto, auto) {
          shard_map_invalidated = true;
        });

    retrier.put(ctx);

    BOOST_CHECK_MESSAGE(shard_map_invalidated,
                        "Shard map should've been invalidated.");
  }
}

BOOST_AUTO_TEST_CASE(WrongShardAndWrongHashrange) {
  // The actual shard will cover hashrange 2 to 10. So the below hash key should fail. 
  std::vector<std::string> arr = {"0", "1", "11"};
  for (const std::string& hashkey : arr) {
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
        )"),
        hashkey);

    bool shard_map_invalidated = false;

    aws::kinesis::core::Retrier retrier(
        std::make_shared<aws::kinesis::core::Configuration>(),
        [&](auto& ur) {
          BOOST_FAIL("Finish should not be called");
        },
        [&](auto& ur) {
          auto& attempts = ur->attempts();
          BOOST_CHECK_EQUAL(attempts.size(), 1);
          BOOST_CHECK(!(bool) attempts[0]);
          BOOST_CHECK_EQUAL(attempts[0].error_code(), "Wrong Shard");
        },
        [&](auto) {
          return std::make_pair(boost::multiprecision::uint128_t(3), boost::multiprecision::uint128_t(10));
        },
        [&](auto, auto) {
          shard_map_invalidated = true;
        });

    retrier.put(ctx);

    BOOST_CHECK_MESSAGE(shard_map_invalidated,
                        "Shard map should've been invalidated.");
  }
}

// AUTO and UNKNOWN streams flow through the Pipeline's solo path, which clears
// predicted_shard. These cases verify the retrier treats such records as
// successful regardless of which shard the service reports, with no retry and
// no shard map invalidation.

namespace {

// Builds a PutRecordsContext whose records have NO predicted shard, optionally
// with no partition key (the full AUTO shape).
auto make_no_predicted_shard_ctx(Aws::Kinesis::Model::PutRecordsOutcome outcome,
                                  bool no_pk = false) {
  auto kr = std::make_shared<aws::kinesis::core::KinesisRecord>();
  auto ur = no_pk ? aws::kinesis::test::make_user_record_no_pk()
                  : aws::kinesis::test::make_user_record_with_hashkey();
  // Note: predicted_shard is intentionally left unset.
  kr->add(ur);
  std::vector<std::shared_ptr<aws::kinesis::core::KinesisRecord>> krs{kr};
  auto ctx = std::make_shared<aws::kinesis::core::PutRecordsContext>(
      "myStream",
      "arn:aws:kinesis:us-east-2:123456789012:stream/myStream",
      "",
      krs);
  ctx->set_outcome(outcome);
  return ctx;
}

const char* kSuccessShard4 = R"(
{
  "FailedRecordCount": 0,
  "Records":[
    {
      "SequenceNumber":"1234",
      "ShardId":"shardId-000000000004"
    }
  ]
}
)";

} // namespace

BOOST_AUTO_TEST_CASE(NoPredictedShard_Succeeds) {
  auto ctx = make_no_predicted_shard_ctx(success_outcome(kSuccessShard4));

  size_t finished = 0;
  aws::kinesis::core::Retrier retrier(
      std::make_shared<aws::kinesis::core::Configuration>(),
      [&](auto& ur) {
        finished++;
        auto& attempts = ur->attempts();
        BOOST_CHECK_EQUAL(attempts.size(), 1);
        BOOST_CHECK((bool) attempts[0]);
        BOOST_CHECK_EQUAL(attempts[0].sequence_number(), "1234");
      },
      [&](auto& ur) {
        BOOST_FAIL("Retry should not be called for a record with no predicted shard");
      },
      [&](auto) {
        return boost::none;
      },
      [&](auto, auto) {
        BOOST_FAIL("Shard map invalidate should not be called");
      });

  retrier.put(ctx);
  BOOST_CHECK_EQUAL(finished, 1);
}

BOOST_AUTO_TEST_CASE(NoPredictedShard_ArbitraryShardId) {
  // Any shard id the service returns is accepted; no "Wrong Shard" path.
  auto ctx = make_no_predicted_shard_ctx(success_outcome(R"(
  {
    "FailedRecordCount": 0,
    "Records":[
      {
        "SequenceNumber":"9999",
        "ShardId":"shardId-000000000042"
      }
    ]
  }
  )"));

  size_t finished = 0;
  aws::kinesis::core::Retrier retrier(
      std::make_shared<aws::kinesis::core::Configuration>(),
      [&](auto& ur) {
        finished++;
        BOOST_CHECK((bool) ur->attempts()[0]);
        BOOST_CHECK_EQUAL(ur->attempts()[0].shard_id(), "shardId-000000000042");
      },
      [&](auto& ur) { BOOST_FAIL("Retry should not be called"); },
      [&](auto) { return boost::none; },
      [&](auto, auto) { BOOST_FAIL("Shard map invalidate should not be called"); });

  retrier.put(ctx);
  BOOST_CHECK_EQUAL(finished, 1);
}

BOOST_AUTO_TEST_CASE(NoPredictedShard_EmptyPK) {
  // Full AUTO shape: no predicted shard and no partition key.
  auto ctx = make_no_predicted_shard_ctx(success_outcome(kSuccessShard4),
                                          /*no_pk=*/true);

  size_t finished = 0;
  aws::kinesis::core::Retrier retrier(
      std::make_shared<aws::kinesis::core::Configuration>(),
      [&](auto& ur) {
        finished++;
        BOOST_CHECK((bool) ur->attempts()[0]);
        BOOST_CHECK(ur->partition_key().empty());
      },
      [&](auto& ur) { BOOST_FAIL("Retry should not be called"); },
      [&](auto) { return boost::none; },
      [&](auto, auto) { BOOST_FAIL("Shard map invalidate should not be called"); });

  retrier.put(ctx);
  BOOST_CHECK_EQUAL(finished, 1);
}

BOOST_AUTO_TEST_SUITE_END()
