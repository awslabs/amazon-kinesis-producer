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
#include <aws/kinesis/core/shard_map.h>
#include <aws/kinesis/model/ListShardsRequest.h>
#include <aws/utils/io_service_executor.h>
#include <aws/utils/utils.h>
#include <aws/core/auth/AWSCredentialsProvider.h>
#include <aws/core/Aws.h>

namespace {

const std::string kStreamName = "myStream";

Aws::Client::ClientConfiguration fake_client_cfg() {
  Aws::Client::ClientConfiguration cfg;
  cfg.region = "us-west-1";
  cfg.endpointOverride = "localhost:61666";
  return cfg;
}

const Aws::Auth::AWSCredentials kEmptyCreds("", "");

template <typename T>
void pop(const std::list<T>* q) {
  ((std::list<T>*) q)->pop_front();
}

class MockKinesisClient : public Aws::Kinesis::KinesisClient {
 public:
  MockKinesisClient(
      std::list<Aws::Kinesis::Model::ListShardsOutcome> outcomes_list_shards,
      std::function<void ()> callback_list_shards = []{})
      : Aws::Kinesis::KinesisClient(kEmptyCreds, fake_client_cfg()),
        outcomes_list_shards_(std::move(outcomes_list_shards)),
        callback_list_shards_(callback_list_shards),
        executor_(std::make_shared<aws::utils::IoServiceExecutor>(1)) {}
  
  virtual void ListShardsAsync(
      const Aws::Kinesis::Model::ListShardsRequest& request,
      const Aws::Kinesis::ListShardsResponseReceivedHandler& handler,
      const std::shared_ptr<const Aws::Client::AsyncCallerContext>& context
          = nullptr) const override {
    executor_->schedule([=] {
    
      if (outcomes_list_shards_.size() == 0) {
        throw std::runtime_error(std::string() + "No outcomes enqueued in the mock"+" list shards");
      }
      auto outcome = outcomes_list_shards_.front();
      pop(&outcomes_list_shards_);
      handler(this, request, outcome, context);
      callback_list_shards_();
    }, std::chrono::milliseconds(20));
  }


 private:
  std::list<Aws::Kinesis::Model::ListShardsOutcome> outcomes_list_shards_;
  std::function<void ()> callback_list_shards_;
  std::shared_ptr<aws::utils::Executor> executor_;
};

class Wrapper {
 public:
  Wrapper(
      std::list<Aws::Kinesis::Model::ListShardsOutcome> outcomes_list_shards,
          int delay = 1500)
      : num_req_received_(0) {
    shard_map_ =
        std::make_shared<aws::kinesis::core::ShardMap>(
            std::make_shared<aws::utils::IoServiceExecutor>(1),
            std::make_shared<MockKinesisClient>(
                outcomes_list_shards,
                [this] { num_req_received_++; }),
            kStreamName,
            std::make_shared<aws::metrics::NullMetricsManager>(),
            std::chrono::milliseconds(100),
            std::chrono::milliseconds(1000));

    aws::utils::sleep_for(std::chrono::milliseconds(delay));
  }

  boost::optional<uint64_t> shard_id(const char* key) {
    return shard_map_->shard_id(
        boost::multiprecision::uint128_t(std::string(key)));
  }

  size_t num_req_received() const {
    return num_req_received_;
  }

  void invalidate(std::chrono::steady_clock::time_point tp, boost::optional<uint64_t> shard_id) {
    shard_map_->invalidate(tp, shard_id);
  }

 private:
  size_t num_req_received_;
  std::shared_ptr<aws::kinesis::core::ShardMap> shard_map_;
};


void init_sdk_if_needed() {
  static bool sdk_initialized = false;
  if (!sdk_initialized) {
    Aws::SDKOptions options;
    options.loggingOptions.logLevel = Aws::Utils::Logging::LogLevel::Info;
    Aws::InitAPI(options);
    sdk_initialized = true;
  }
}

template <class R, class O> O success_outcome(std::string json) {
  init_sdk_if_needed();
  Aws::Utils::Json::JsonValue j(json);
  Aws::Http::HeaderValueCollection h;
  Aws::AmazonWebServiceResult<Aws::Utils::Json::JsonValue> awsr(j, h);
  R result(awsr);
  O outcome(result);
  return outcome;
}


template <class O> O error_outcome() {
  init_sdk_if_needed();
  O outcome(
      Aws::Client::AWSError<Aws::Kinesis::KinesisErrors>(
          Aws::Kinesis::KinesisErrors::UNKNOWN,
          "test"));
  return outcome;
}

} //namespace

BOOST_AUTO_TEST_SUITE(ShardMap)

BOOST_AUTO_TEST_CASE(Basic) {
  std::list<Aws::Kinesis::Model::ListShardsOutcome> outcomes_list_shards;

  outcomes_list_shards.push_back(
        success_outcome<Aws::Kinesis::Model::ListShardsResult,Aws::Kinesis::Model::ListShardsOutcome>(R"XXXX({
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
  })XXXX"));

  Wrapper wrapper(outcomes_list_shards);

  BOOST_CHECK_EQUAL(
      *wrapper.shard_id("170141183460469231731687303715884105728"),
      1);
  BOOST_CHECK_EQUAL(
      *wrapper.shard_id("340282366920938463463374607431768211455"),
      1);
  BOOST_CHECK_EQUAL(
      *wrapper.shard_id("0"),
      2);
  BOOST_CHECK_EQUAL(
      *wrapper.shard_id("85070591730234615865843651857942052862"),
      2);
  BOOST_CHECK_EQUAL(
      *wrapper.shard_id("85070591730234615865843651857942052863"),
      3);
  BOOST_CHECK_EQUAL(
      *wrapper.shard_id("170141183460469231731687303715884105727"),
      3);
  BOOST_CHECK_EQUAL(
      wrapper.num_req_received(),
      1);

}


BOOST_AUTO_TEST_CASE(ClosedShards) {
  std::list<Aws::Kinesis::Model::ListShardsOutcome> outcomes_list_shards;
  outcomes_list_shards.push_back(
        success_outcome<Aws::Kinesis::Model::ListShardsResult,Aws::Kinesis::Model::ListShardsOutcome>(R"XXXX({
      "Shards": [
        {
          "HashKeyRange": {
            "EndingHashKey": "340282366920938463463374607431768211455",
            "StartingHashKey": "170141183460469231731687303715884105728"
          },
          "ShardId": "shardId-000000000001",
          "SequenceNumberRange": {
            "EndingSequenceNumber": "49549167410956685081233009822320176730553508082787287058",
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
        },
        {
          "HashKeyRange": {
            "EndingHashKey": "270141183460469231731687303715884105727",
            "StartingHashKey": "170141183460469231731687303715884105728"
          },
          "ShardId": "shardId-000000000004",
          "ParentShardId": "shardId-000000000001",
          "SequenceNumberRange": {
            "StartingSequenceNumber": "49549295168948777979169149491056351269437634281436348482"
          }
        },
        {
          "HashKeyRange": {
            "EndingHashKey": "340282366920938463463374607431768211455",
            "StartingHashKey": "270141183460469231731687303715884105728"
          },
          "ShardId": "shardId-000000000005",
          "ParentShardId": "shardId-000000000001",
          "SequenceNumberRange": {
            "StartingSequenceNumber": "49549295168971078724367680114197886987710282642942328914"
          }
        }
      ]
  })XXXX"));

  Wrapper wrapper(outcomes_list_shards);

  BOOST_CHECK_EQUAL(
      *wrapper.shard_id("0"),
      2);
  BOOST_CHECK_EQUAL(
      *wrapper.shard_id("85070591730234615865843651857942052862"),
      2);
  BOOST_CHECK_EQUAL(
      *wrapper.shard_id("85070591730234615865843651857942052863"),
      3);
  BOOST_CHECK_EQUAL(
      *wrapper.shard_id("170141183460469231731687303715884105727"),
      3);
  BOOST_CHECK_EQUAL(
      *wrapper.shard_id("170141183460469231731687303715884105728"),
      4);
  BOOST_CHECK_EQUAL(
      *wrapper.shard_id("270141183460469231731687303715884105728"),
      5);
  BOOST_CHECK_EQUAL(
      *wrapper.shard_id("340282366920938463463374607431768211455"),
      5);
  BOOST_CHECK_EQUAL(
      wrapper.num_req_received(),
      1);
}



BOOST_AUTO_TEST_CASE(PaginatedResults) {
  std::list<Aws::Kinesis::Model::ListShardsOutcome> outcomes_list_shards;

  outcomes_list_shards.push_back(
        success_outcome<Aws::Kinesis::Model::ListShardsResult,Aws::Kinesis::Model::ListShardsOutcome>(R"XXXX({
      "Shards": [
        {
          "HashKeyRange": {
            "EndingHashKey": "340282366920938463463374607431768211455",
            "StartingHashKey": "170141183460469231731687303715884105728"
          },
          "ShardId": "shardId-000000000001",
          "SequenceNumberRange": {
            "EndingSequenceNumber": "49549167410956685081233009822320176730553508082787287058",
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
        }
      ],
      "NextToken": "AAAAAAAAAAG0QcUm4uaCES69GuO6gBMdI+3lpu8FX/xFCUQU1rXHjqjDusPzyT3TIGQLTyzvBzR71j49xYeKJCtlQB9ZX8n8iCtdPHd7abVO4vc4Oc/KboHWEUsPzGgi5A9DN1qZO5+Rl6wEhlRapOIVHXwF/l6Fmah9Ie1iSUy5t1G2sL+WAZ0VU6y54EWAcAPQIISk1X7XZIWl9/ODi9zCHz6azeZI"
    })XXXX"));

  outcomes_list_shards.push_back(
        success_outcome<Aws::Kinesis::Model::ListShardsResult,Aws::Kinesis::Model::ListShardsOutcome>(R"XXXX({
      "Shards": [
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
        },
        {
          "HashKeyRange": {
            "EndingHashKey": "270141183460469231731687303715884105727",
            "StartingHashKey": "170141183460469231731687303715884105728"
          },
          "ShardId": "shardId-000000000004",
          "ParentShardId": "shardId-000000000001",
          "SequenceNumberRange": {
            "StartingSequenceNumber": "49549295168948777979169149491056351269437634281436348482"
          }
        },
        {
          "HashKeyRange": {
            "EndingHashKey": "340282366920938463463374607431768211455",
            "StartingHashKey": "270141183460469231731687303715884105728"
          },
          "ShardId": "shardId-000000000005",
          "ParentShardId": "shardId-000000000001",
          "SequenceNumberRange": {
            "StartingSequenceNumber": "49549295168971078724367680114197886987710282642942328914"
          }
        }
      ]
    })XXXX"));

  Wrapper wrapper(outcomes_list_shards);


  BOOST_CHECK_EQUAL(
      *wrapper.shard_id("0"),
      2);
  BOOST_CHECK_EQUAL(
      *wrapper.shard_id("85070591730234615865843651857942052862"),
      2);
  BOOST_CHECK_EQUAL(
      *wrapper.shard_id("85070591730234615865843651857942052863"),
      3);
  BOOST_CHECK_EQUAL(
      *wrapper.shard_id("170141183460469231731687303715884105727"),
      3);
  BOOST_CHECK_EQUAL(
      *wrapper.shard_id("170141183460469231731687303715884105728"),
      4);
  BOOST_CHECK_EQUAL(
      *wrapper.shard_id("270141183460469231731687303715884105728"),
      5);
  BOOST_CHECK_EQUAL(
      *wrapper.shard_id("340282366920938463463374607431768211455"),
      5);

  BOOST_CHECK_EQUAL(
      wrapper.num_req_received(),
      2);
}


BOOST_AUTO_TEST_CASE(RetryAfterInitialFailure) {
  std::list<Aws::Kinesis::Model::ListShardsOutcome> outcomes_list_shards;
  
  outcomes_list_shards.push_back(error_outcome<Aws::Kinesis::Model::ListShardsOutcome>());
  outcomes_list_shards.push_back(
        success_outcome<Aws::Kinesis::Model::ListShardsResult,Aws::Kinesis::Model::ListShardsOutcome>(R"XXXX({
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
    })XXXX"));

  Wrapper wrapper(outcomes_list_shards);

  BOOST_CHECK_EQUAL(
      *wrapper.shard_id("170141183460469231731687303715884105728"),
      1);
  BOOST_CHECK_EQUAL(
      *wrapper.shard_id("340282366920938463463374607431768211455"),
      1);
  BOOST_CHECK_EQUAL(
      *wrapper.shard_id("0"),
      2);
  BOOST_CHECK_EQUAL(
      *wrapper.shard_id("85070591730234615865843651857942052862"),
      2);
  BOOST_CHECK_EQUAL(
      *wrapper.shard_id("85070591730234615865843651857942052863"),
      3);
  BOOST_CHECK_EQUAL(
      *wrapper.shard_id("170141183460469231731687303715884105727"),
      3);

  BOOST_CHECK_EQUAL(
      wrapper.num_req_received(),
      2);
}


BOOST_AUTO_TEST_CASE(RetryWithFailureInTheMiddle) {
  std::list<Aws::Kinesis::Model::ListShardsOutcome> outcomes_list_shards;
  outcomes_list_shards.push_back(
        success_outcome<Aws::Kinesis::Model::ListShardsResult,Aws::Kinesis::Model::ListShardsOutcome>(R"XXXX({
      "Shards": [
        {
          "HashKeyRange": {
            "EndingHashKey": "340282366920938463463374607431768211455",
            "StartingHashKey": "170141183460469231731687303715884105728"
          },
          "ShardId": "shardId-000000000001",
          "SequenceNumberRange": {
            "EndingSequenceNumber": "49549167410956685081233009822320176730553508082787287058",
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
        }
      ],
      "NextToken": "AAAAAAAAAAG0QcUm4uaCES69GuO6gBMdI+3lpu8FX/xFCUQU1rXHjqjDusPzyT3TIGQLTyzvBzR71j49xYeKJCtlQB9ZX8n8iCtdPHd7abVO4vc4Oc/KboHWEUsPzGgi5A9DN1qZO5+Rl6wEhlRapOIVHXwF/l6Fmah9Ie1iSUy5t1G2sL+WAZ0VU6y54EWAcAPQIISk1X7XZIWl9/ODi9zCHz6azeZI"
    })XXXX"));
  
  outcomes_list_shards.push_back(error_outcome<Aws::Kinesis::Model::ListShardsOutcome>());
  outcomes_list_shards.push_back(error_outcome<Aws::Kinesis::Model::ListShardsOutcome>());

  outcomes_list_shards.push_back(outcomes_list_shards.front());

  outcomes_list_shards.push_back(
        success_outcome<Aws::Kinesis::Model::ListShardsResult,Aws::Kinesis::Model::ListShardsOutcome>(R"XXXX({
      "Shards": [
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
        },
        {
          "HashKeyRange": {
            "EndingHashKey": "270141183460469231731687303715884105727",
            "StartingHashKey": "170141183460469231731687303715884105728"
          },
          "ShardId": "shardId-000000000004",
          "ParentShardId": "shardId-000000000001",
          "SequenceNumberRange": {
            "StartingSequenceNumber": "49549295168948777979169149491056351269437634281436348482"
          }
        },
        {
          "HashKeyRange": {
            "EndingHashKey": "340282366920938463463374607431768211455",
            "StartingHashKey": "270141183460469231731687303715884105728"
          },
          "ShardId": "shardId-000000000005",
          "ParentShardId": "shardId-000000000001",
          "SequenceNumberRange": {
            "StartingSequenceNumber": "49549295168971078724367680114197886987710282642942328914"
          }
        }
      ]
    })XXXX"));

  Wrapper wrapper(outcomes_list_shards);


  BOOST_CHECK_EQUAL(
      *wrapper.shard_id("0"),
      2);
  BOOST_CHECK_EQUAL(
      *wrapper.shard_id("85070591730234615865843651857942052862"),
      2);
  BOOST_CHECK_EQUAL(
      *wrapper.shard_id("85070591730234615865843651857942052863"),
      3);
  BOOST_CHECK_EQUAL(
      *wrapper.shard_id("170141183460469231731687303715884105727"),
      3);
  BOOST_CHECK_EQUAL(
      *wrapper.shard_id("170141183460469231731687303715884105728"),
      4);
  BOOST_CHECK_EQUAL(
      *wrapper.shard_id("270141183460469231731687303715884105728"),
      5);
  BOOST_CHECK_EQUAL(
      *wrapper.shard_id("340282366920938463463374607431768211455"),
      5);

  BOOST_CHECK_EQUAL(
      wrapper.num_req_received(),
      5);
}



BOOST_AUTO_TEST_CASE(Backoff) {
  std::list<Aws::Kinesis::Model::ListShardsOutcome> outcomes_list_shards;
  for (int i = 0; i < 25; i++) {
    outcomes_list_shards.push_back(error_outcome<Aws::Kinesis::Model::ListShardsOutcome>());
  }

  Wrapper wrapper(outcomes_list_shards, 0);

  auto start = std::chrono::high_resolution_clock::now();

  // We have initial backoff = 100, growth factor = 1.5, so the 6th attempt
  // should happen 1317ms after the 1st attempt.
  while (wrapper.num_req_received() < 6) {
    aws::this_thread::yield();
  }
  BOOST_CHECK_CLOSE(aws::utils::seconds_since(start), 1.317, 20);

  // The backoff should reach a cap of 1000ms, so after 5 more seconds, there
  // should be 5 additional attempts, for a total of 11.
  while (wrapper.num_req_received() < 11) {
    aws::this_thread::yield();
  }
  BOOST_CHECK_CLOSE(aws::utils::seconds_since(start), 6.317, 20);

  aws::utils::sleep_for(std::chrono::milliseconds(500));
}


BOOST_AUTO_TEST_CASE(Invalidate) {
  std::list<Aws::Kinesis::Model::ListShardsOutcome> outcomes_list_shards;
  outcomes_list_shards.push_back(
        success_outcome<Aws::Kinesis::Model::ListShardsResult,Aws::Kinesis::Model::ListShardsOutcome>(R"XXXX({
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
  })XXXX"));

  
  outcomes_list_shards.push_back(
        success_outcome<Aws::Kinesis::Model::ListShardsResult,Aws::Kinesis::Model::ListShardsOutcome>(R"XXXX({
      "Shards": [
        {
          "HashKeyRange": {
            "EndingHashKey": "340282366920938463463374607431768211455",
            "StartingHashKey": "170141183460469231731687303715884105728"
          },
          "ShardId": "shardId-000000000005",
          "SequenceNumberRange": {
            "StartingSequenceNumber": "49549167410945534708633744510750617797212193316405248018"
          }
        },
        {
          "HashKeyRange": {
            "EndingHashKey": "85070591730234615865843651857942052862",
            "StartingHashKey": "0"
          },
          "ShardId": "shardId-000000000006",
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
          "ShardId": "shardId-000000000007",
          "ParentShardId": "shardId-000000000000",
          "SequenceNumberRange": {
            "StartingSequenceNumber": "49549169978965547300229121751154719765762108750148141106"
          }
        }
      ]
  })XXXX"));

  Wrapper wrapper(outcomes_list_shards);

  // Calling invalidate with a timestamp that's before the last update should
  // not actually invalidate the shard map.
  wrapper.invalidate(
      std::chrono::steady_clock::now() - std::chrono::seconds(15), {});

  // Shard map should continue working
  BOOST_CHECK_EQUAL(
      *wrapper.shard_id("170141183460469231731687303715884105728"),
      1);
  BOOST_CHECK_EQUAL(
      *wrapper.shard_id("340282366920938463463374607431768211455"),
      1);
  BOOST_CHECK_EQUAL(
      *wrapper.shard_id("0"),
      2);
  BOOST_CHECK_EQUAL(
      *wrapper.shard_id("85070591730234615865843651857942052862"),
      2);
  BOOST_CHECK_EQUAL(
      *wrapper.shard_id("85070591730234615865843651857942052863"),
      3);
  BOOST_CHECK_EQUAL(
      *wrapper.shard_id("170141183460469231731687303715884105727"),
      3);


  // On the other hand, calling invalidate with a timestamp after the last
  // update should actually invalidate it and trigger an update.
  wrapper.invalidate(std::chrono::steady_clock::now(), {});

  BOOST_CHECK(!wrapper.shard_id("0"));

  // Calling invalidate again during update should not trigger more requests.
  for (int i = 0; i < 5; i++) {
    wrapper.invalidate(std::chrono::steady_clock::now(), {});
    aws::utils::sleep_for(std::chrono::milliseconds(2));
  }

  BOOST_CHECK(!wrapper.shard_id("0"));

  aws::utils::sleep_for(std::chrono::milliseconds(500));

  // A new shard map should've been fetched
  BOOST_CHECK_EQUAL(
      *wrapper.shard_id("170141183460469231731687303715884105728"),
      5);
  BOOST_CHECK_EQUAL(
      *wrapper.shard_id("340282366920938463463374607431768211455"),
      5);
  BOOST_CHECK_EQUAL(
      *wrapper.shard_id("0"),
      6);
  BOOST_CHECK_EQUAL(
      *wrapper.shard_id("85070591730234615865843651857942052862"),
      6);
  BOOST_CHECK_EQUAL(
      *wrapper.shard_id("85070591730234615865843651857942052863"),
      7);
  BOOST_CHECK_EQUAL(
      *wrapper.shard_id("170141183460469231731687303715884105727"),
      7);

  BOOST_CHECK_EQUAL(
      wrapper.num_req_received(),
      2);
}


BOOST_AUTO_TEST_CASE(InvalidateWithShard) {
  std::list<Aws::Kinesis::Model::ListShardsOutcome> outcomes_list_shards;
  outcomes_list_shards.push_back(
        success_outcome<Aws::Kinesis::Model::ListShardsResult,Aws::Kinesis::Model::ListShardsOutcome>(R"XXXX({
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
  })XXXX"));

  
  outcomes_list_shards.push_back(
        success_outcome<Aws::Kinesis::Model::ListShardsResult,Aws::Kinesis::Model::ListShardsOutcome>(R"XXXX({
      "Shards": [
        {
          "HashKeyRange": {
            "EndingHashKey": "340282366920938463463374607431768211455",
            "StartingHashKey": "170141183460469231731687303715884105728"
          },
          "ShardId": "shardId-000000000005",
          "SequenceNumberRange": {
            "StartingSequenceNumber": "49549167410945534708633744510750617797212193316405248018"
          }
        },
        {
          "HashKeyRange": {
            "EndingHashKey": "85070591730234615865843651857942052862",
            "StartingHashKey": "0"
          },
          "ShardId": "shardId-000000000006",
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
          "ShardId": "shardId-000000000007",
          "ParentShardId": "shardId-000000000000",
          "SequenceNumberRange": {
            "StartingSequenceNumber": "49549169978965547300229121751154719765762108750148141106"
          }
        }
      ]
  })XXXX"));

  Wrapper wrapper(outcomes_list_shards);

  //Invalidating even after the shard map has updated but the shard is closed
  //should not result in an update
  wrapper.invalidate(
      std::chrono::steady_clock::now(), boost::optional<uint64_t>(0));

  // Shard map should continue working
  BOOST_CHECK_EQUAL(
      *wrapper.shard_id("170141183460469231731687303715884105728"),
      1);
  BOOST_CHECK_EQUAL(
      *wrapper.shard_id("340282366920938463463374607431768211455"),
      1);
  BOOST_CHECK_EQUAL(
      *wrapper.shard_id("0"),
      2);
  BOOST_CHECK_EQUAL(
      *wrapper.shard_id("85070591730234615865843651857942052862"),
      2);
  BOOST_CHECK_EQUAL(
      *wrapper.shard_id("85070591730234615865843651857942052863"),
      3);
  BOOST_CHECK_EQUAL(
      *wrapper.shard_id("170141183460469231731687303715884105727"),
      3);

  //Invalidating even after the shard map has updated but if the shard is open
  //it should result in an update
  wrapper.invalidate(std::chrono::steady_clock::now(), boost::optional<uint64_t>(1));

  BOOST_CHECK(!wrapper.shard_id("0"));

  // Calling invalidate again during update should not trigger more requests.
  for (int i = 0; i < 5; i++) {
    wrapper.invalidate(std::chrono::steady_clock::now(), {});
    aws::utils::sleep_for(std::chrono::milliseconds(2));
  }

  BOOST_CHECK(!wrapper.shard_id("0"));

  aws::utils::sleep_for(std::chrono::milliseconds(500));

  // A new shard map should've been fetched
  BOOST_CHECK_EQUAL(
      *wrapper.shard_id("170141183460469231731687303715884105728"),
      5);
  BOOST_CHECK_EQUAL(
      *wrapper.shard_id("340282366920938463463374607431768211455"),
      5);
  BOOST_CHECK_EQUAL(
      *wrapper.shard_id("0"),
      6);
  BOOST_CHECK_EQUAL(
      *wrapper.shard_id("85070591730234615865843651857942052862"),
      6);
  BOOST_CHECK_EQUAL(
      *wrapper.shard_id("85070591730234615865843651857942052863"),
      7);
  BOOST_CHECK_EQUAL(
      *wrapper.shard_id("170141183460469231731687303715884105727"),
      7);

  BOOST_CHECK_EQUAL(
      wrapper.num_req_received(),
      2);
}




BOOST_AUTO_TEST_SUITE_END()
