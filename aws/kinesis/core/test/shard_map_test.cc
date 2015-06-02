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

#include <aws/kinesis/core/shard_map.h>
#include <aws/kinesis/test/test_tls_server.h>
#include <aws/utils/json.h>
#include <aws/utils/io_service_executor.h>
#include <aws/http/io_service_socket.h>
#include <aws/utils/utils.h>

namespace {

const int kPort = aws::kinesis::test::TestTLSServer::kDefaultPort;
const std::string kStreamName = "myStream";

class Wrapper {
 public:
  Wrapper(int delay = 100)
      : socket_factory_(std::make_shared<aws::http::IoServiceSocketFactory>()),
        executor_(std::make_shared<aws::utils::IoServiceExecutor>(1)),
        http_client_(
            std::make_shared<aws::http::HttpClient>(
                executor_,
                socket_factory_,
                "localhost",
                kPort,
                true,
                false)),
        creds_(
            std::make_shared<aws::auth::BasicAwsCredentialsProvider>(
                "AKIAAAAAAAAAAAAAAAAA",
                "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa")),
        shard_map_(executor_,
                   http_client_,
                   creds_,
                   "us-west-1",
                   kStreamName,
                   std::make_shared<aws::metrics::NullMetricsManager>(),
                   std::chrono::milliseconds(100),
                   std::chrono::milliseconds(1000)) {
    aws::utils::sleep_for(std::chrono::milliseconds(delay));
  }

  boost::optional<uint64_t> shard_id(const char* key) {
    return
        shard_map_.shard_id(boost::multiprecision::uint128_t(std::string(key)));
  }

  void invalidate(std::chrono::steady_clock::time_point tp) {
    shard_map_.invalidate(tp);
  }

 private:
  std::shared_ptr<aws::http::SocketFactory> socket_factory_;
  std::shared_ptr<aws::utils::Executor> executor_;
  std::shared_ptr<aws::http::HttpClient> http_client_;
  std::shared_ptr<aws::auth::BasicAwsCredentialsProvider> creds_;
  aws::kinesis::core::ShardMap shard_map_;
};

auto make_handler(std::string content,
                  std::string expected_sid = "",
                  int code = 200,
                  int delay = 3) {
  return [=](auto& req) {
    auto json = aws::utils::Json(req.data());

    BOOST_CHECK_EQUAL((std::string) json["StreamName"], kStreamName);
    if (!expected_sid.empty()) {
      BOOST_CHECK_EQUAL((std::string) json["ExclusiveStartShardId"],
                        expected_sid);
    }

    bool has_signature = false;
    for (const auto& header : req.headers()) {
      if (header.first == "Authorization") {
        has_signature = true;
      }
    }
    BOOST_CHECK_MESSAGE(has_signature, "Request should've been signed");

    aws::http::HttpResponse res(code);
    res.set_data(content);
    aws::utils::sleep_for(std::chrono::milliseconds(delay));
    return res;
  };
}

} //namespace

BOOST_AUTO_TEST_SUITE(ShardMap)

BOOST_AUTO_TEST_CASE(Basic) {
  aws::kinesis::test::TestTLSServer server;

  server.enqueue_handler(make_handler(R"XXXX(
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
  )XXXX"));

  Wrapper wrapper;

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
}

BOOST_AUTO_TEST_CASE(ClosedShards) {
  aws::kinesis::test::TestTLSServer server;

  server.enqueue_handler(make_handler(R"XXXX(
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
    }
  }
  )XXXX"));

  Wrapper wrapper;

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
}

BOOST_AUTO_TEST_CASE(PaginatedResults) {
  aws::kinesis::test::TestTLSServer server;

  server.enqueue_handler(make_handler(R"XXXX(
  {
    "StreamDescription": {
      "HasMoreShards": true,
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
      ]
    }
  }
  )XXXX"));

  auto body = R"XXXX(
  {
    "StreamDescription": {
      "HasMoreShards": false,
      "StreamStatus": "ACTIVE",
      "StreamName": "test",
      "StreamARN": "arn:aws:kinesis:us-west-2:263868185958:stream\/test",
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
    }
  }
  )XXXX";
  server.enqueue_handler(make_handler(body, "shardId-000000000002"));

  Wrapper wrapper;

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
}

BOOST_AUTO_TEST_CASE(Retry) {
  aws::kinesis::test::TestTLSServer server;

  server.enqueue_handler(make_handler("expected, don't be alarmed", "", 500));

  // This will cause an exception during parsing
  server.enqueue_handler(make_handler(".."));

  server.enqueue_handler(make_handler(R"XXXX(
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
  )XXXX"));

  Wrapper wrapper(500);

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
}

BOOST_AUTO_TEST_CASE(Backoff) {
  aws::kinesis::test::TestTLSServer server;

  std::atomic<int> count(0);

  for (int i = 0; i < 25; i++) {
    server.enqueue_handler([&](auto& req) {
      count++;
      return make_handler("expected, don't be alarmed", "", 500)(req);
    });
  }

  Wrapper wrapper(0);

  // We have initial backoff = 100, growth factor = 1.5, so after 1317ms
  // there should be 6 attempts.
  aws::utils::sleep_for(std::chrono::milliseconds(1400));
  BOOST_CHECK_EQUAL(count, 6);

  // The backoff should reach a cap of 1000ms, so after 5 more seconds, there
  // should be 5 additional attempts, for a total of 11.
  aws::utils::sleep_for(std::chrono::milliseconds(5100));
  BOOST_CHECK_EQUAL(count, 11);
}

BOOST_AUTO_TEST_CASE(Invalidate) {
  aws::kinesis::test::TestTLSServer server;

  server.enqueue_handler(make_handler(R"XXXX(
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
  )XXXX"));

  server.enqueue_handler(make_handler(R"XXXX(
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
    }
  }
  )XXXX", "", 200, 50));

  Wrapper wrapper;

  // Calling invalidate with a timestamp that's before the last update should
  // not actually invalidate the shard map.
  wrapper.invalidate(
      std::chrono::steady_clock::now() - std::chrono::milliseconds(500));

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
  wrapper.invalidate(std::chrono::steady_clock::now());

  BOOST_CHECK(!wrapper.shard_id("0"));

  // Calling invalidate again during update should not trigger more requests.
  for (int i = 0; i < 5; i++) {
    wrapper.invalidate(std::chrono::steady_clock::now());
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
}

BOOST_AUTO_TEST_SUITE_END()
