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

#include <boost/test/unit_test.hpp>

#include <aws/utils/logging.h>

#include <aws/kinesis/core/retrier.h>

#include <aws/kinesis/core/test/test_utils.h>

namespace {

using TimePoint = std::chrono::steady_clock::time_point;

auto make_result(
    std::shared_ptr<aws::kinesis::core::PutRecordsRequest> prr,
    std::unique_ptr<aws::http::HttpResponse>&& response,
    TimePoint start = std::chrono::steady_clock::now(),
    TimePoint end = std::chrono::steady_clock::now()) {
  return std::make_shared<aws::http::HttpResult>(
      std::move(response),
      std::move(prr),
      start,
      end);
}

auto make_put_records_request(size_t num_kr, size_t num_ur_per_kr) {
  auto prr = std::make_shared<aws::kinesis::core::PutRecordsRequest>();
  for (size_t i = 0; i < num_kr; i++) {
    auto kr = std::make_shared<aws::kinesis::core::KinesisRecord>();
    for (size_t j = 0; j < num_ur_per_kr; j++) {
      auto ur = aws::kinesis::test::make_user_record();
      ur->predicted_shard(i);
      kr->add(ur);
    }
    prr->add(kr);
  }
  return prr;
}

} //namespace

BOOST_AUTO_TEST_SUITE(Retrier)

// Case where there are no errors
BOOST_AUTO_TEST_CASE(Success) {
  auto response = std::make_unique<aws::http::HttpResponse>(200);
  response->set_data(R"(
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
  )");

  auto num_ur_per_kr = 10;
  auto num_kr = 2;
  auto prr = make_put_records_request(num_kr, num_ur_per_kr);

  auto start = std::chrono::steady_clock::now();
  auto end = start + std::chrono::milliseconds(5);
  auto result = make_result(prr, std::move(response), start, end);

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

  retrier.put(result);

  BOOST_CHECK_EQUAL(count, num_kr * num_ur_per_kr);
}

BOOST_AUTO_TEST_CASE(Code500) {
  auto response = std::make_unique<aws::http::HttpResponse>(500);
  auto prr = make_put_records_request(1, 10);
  auto start = std::chrono::steady_clock::now();
  auto end = start + std::chrono::milliseconds(5);
  auto result = make_result(prr, std::move(response), start, end);

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
        BOOST_CHECK_EQUAL(attempts[0].error_code(), "500");
        BOOST_CHECK_EQUAL(attempts[0].error_message(), "");
      },
      [&](auto) {
        BOOST_FAIL("Shard map invalidate should not be called");
      });

  retrier.put(result);

  BOOST_CHECK_EQUAL(count, 10);
}

BOOST_AUTO_TEST_CASE(Code400) {
  auto err_msg = "forbidden";
  auto response = std::make_unique<aws::http::HttpResponse>(403);
  response->set_data(err_msg);

  auto prr = make_put_records_request(1, 10);
  auto result = make_result(prr, std::move(response));

  size_t count = 0;
  aws::kinesis::core::Retrier retrier(
      std::make_shared<aws::kinesis::core::Configuration>(),
      [&](auto& ur) {
        count++;
        auto& attempts = ur->attempts();
        BOOST_CHECK_EQUAL(attempts.size(), 1);
        BOOST_CHECK(!(bool) attempts[0]);
        BOOST_CHECK_EQUAL(attempts[0].error_code(), "403");
        BOOST_CHECK_EQUAL(attempts[0].error_message(), err_msg);
      },
      [&](auto& ur) {
        BOOST_FAIL("Retry should not be called");
      },
      [&](auto) {
        BOOST_FAIL("Shard map invalidate should not be called");
      });

  retrier.put(result);

  BOOST_CHECK_EQUAL(count, 10);
}

// A mix of success and failures in a PutRecordsResult.
BOOST_AUTO_TEST_CASE(Partial) {
  auto response = std::make_unique<aws::http::HttpResponse>(200);
  response->set_data(R"(
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
  )");

  auto num_ur_per_kr = 10;
  auto num_kr = 6;
  auto prr = make_put_records_request(num_kr, num_ur_per_kr);
  auto result = make_result(prr, std::move(response));

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

  retrier.put(result);

  BOOST_CHECK_EQUAL(count, num_kr * num_ur_per_kr);
}

BOOST_AUTO_TEST_CASE(FailIfThrottled) {
  auto response = std::make_unique<aws::http::HttpResponse>(200);
  response->set_data(R"(
  {
    "FailedRecordCount": 1,
    "Records":[
      {
        "ErrorCode":"ProvisionedThroughputExceededException",
        "ErrorMessage":"..."
      }
    ]
  }
  )");

  auto prr = make_put_records_request(1, 10);
  auto result = make_result(prr, std::move(response));

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

  retrier.put(result);

  BOOST_CHECK_EQUAL(count, 10);
}

BOOST_AUTO_TEST_CASE(Exception) {
  auto result =
    std::make_shared<aws::http::HttpResult>(
        "some error",
         make_put_records_request(1, 10));

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
        BOOST_CHECK(!(bool) attempts[0]);
        BOOST_CHECK_EQUAL(attempts[0].error_code(), "Exception");
        BOOST_CHECK_EQUAL(attempts[0].error_message(), "some error");
      },
      [&](auto) {
        BOOST_FAIL("Shard map invalidate should not be called");
      });

  retrier.put(result);

  BOOST_CHECK_EQUAL(count, 10);
}

BOOST_AUTO_TEST_CASE(WrongShard) {
  auto response = std::make_unique<aws::http::HttpResponse>(200);
  response->set_data(R"(
  {
    "FailedRecordCount": 0,
    "Records":[
      {
        "SequenceNumber":"1234",
        "ShardId":"shardId-000000000004"
      }
    ]
  }
  )");

  auto prr = make_put_records_request(1, 1);
  auto result = make_result(prr, std::move(response));

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

  retrier.put(result);

  BOOST_CHECK_MESSAGE(shard_map_invalidated,
                      "Shard map should've been invalidated.");
  BOOST_CHECK_EQUAL(count, 1);
}

BOOST_AUTO_TEST_SUITE_END()
