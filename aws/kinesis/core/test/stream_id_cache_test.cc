#include <boost/test/unit_test.hpp>

#include <aws/kinesis/core/put_records_context.h>
#include <unordered_map>
#include <functional>

namespace {

const std::string kStreamName = "myStream";
const std::string kStreamARN = "arn:aws:kinesis:us-east-2:123456789012:stream/myStream";
const std::string kStreamId = "test-stream-id-12345";

} // namespace

BOOST_AUTO_TEST_SUITE(StreamIdCache)

struct CacheFixture {
  CacheFixture() {
    cache[kStreamName] = kStreamId;
    cache["stream1"] = "id1";
    cache["stream2"] = "id2";
    cache["stream3"] = "id3";

    get_stream_id = [this](const std::string& stream_name) {
      auto it = cache.find(stream_name);
      return (it != cache.end()) ? it->second : std::string("");
    };
  }
  
  std::unordered_map<std::string, std::string> cache;
  std::function<std::string(const std::string&)> get_stream_id;
};

BOOST_FIXTURE_TEST_CASE(CacheHit_RequestHasStreamId, CacheFixture) {
  std::string stream_id = get_stream_id(kStreamName);

  std::vector<std::shared_ptr<aws::kinesis::core::KinesisRecord>> items;
  auto context = std::make_shared<aws::kinesis::core::PutRecordsContext>(
      kStreamName, kStreamARN, stream_id, items);
  auto request = context->to_sdk_request();

  BOOST_CHECK_EQUAL(request.GetStreamId(), kStreamId);
}

BOOST_FIXTURE_TEST_CASE(CacheMiss_RequestHasNoStreamId, CacheFixture) {
  std::string stream_id = get_stream_id("nonexistent-stream");

  std::vector<std::shared_ptr<aws::kinesis::core::KinesisRecord>> items;
  auto context = std::make_shared<aws::kinesis::core::PutRecordsContext>(
      "nonexistent-stream", kStreamARN, stream_id, items);
  auto request = context->to_sdk_request();

  BOOST_CHECK(request.GetStreamId().empty());
}

BOOST_FIXTURE_TEST_CASE(MultipleStreams_CorrectStreamIds, CacheFixture) {
  std::vector<std::shared_ptr<aws::kinesis::core::KinesisRecord>> items;

  auto context1 = std::make_shared<aws::kinesis::core::PutRecordsContext>(
      "stream1", kStreamARN, cache["stream1"], items);
  BOOST_CHECK_EQUAL(context1->to_sdk_request().GetStreamId(), "id1");
  auto context2 = std::make_shared<aws::kinesis::core::PutRecordsContext>(
      "stream2", kStreamARN, cache["stream2"], items);
  BOOST_CHECK_EQUAL(context2->to_sdk_request().GetStreamId(), "id2");

  auto context3 = std::make_shared<aws::kinesis::core::PutRecordsContext>(
      "stream3", kStreamARN, cache["stream3"], items);
  BOOST_CHECK_EQUAL(context3->to_sdk_request().GetStreamId(), "id3");
}

BOOST_FIXTURE_TEST_CASE(RealFlow_LambdaCallbackRetrievesFromCache, CacheFixture) {
  std::vector<std::shared_ptr<aws::kinesis::core::KinesisRecord>> items;

  std::string stream_id = get_stream_id(kStreamName);

  BOOST_CHECK_EQUAL(stream_id, kStreamId);

  auto context = std::make_shared<aws::kinesis::core::PutRecordsContext>(
      kStreamName, kStreamARN, stream_id, items);
  auto request = context->to_sdk_request();

  BOOST_CHECK_EQUAL(request.GetStreamId(), kStreamId);

  std::string missing_id = get_stream_id("nonexistent");
  BOOST_CHECK(missing_id.empty());
}

BOOST_AUTO_TEST_SUITE_END()
