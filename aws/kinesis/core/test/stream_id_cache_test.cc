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

// Test fixture with common cache setup
struct CacheFixture {
  CacheFixture() {
    // Setup: Add entries to cache
    cache[kStreamName] = kStreamId;
    cache["stream1"] = "id1";
    cache["stream2"] = "id2";
    cache["stream3"] = "id3";
    
    // Setup: Create lambda getter (like KinesisProducer does)
    get_stream_id = [this](const std::string& stream_name) {
      auto it = cache.find(stream_name);
      return (it != cache.end()) ? it->second : std::string("");
    };
  }
  
  std::unordered_map<std::string, std::string> cache;
  std::function<std::string(const std::string&)> get_stream_id;
};

// Test 1: Cache hit - request includes stream ID
BOOST_FIXTURE_TEST_CASE(CacheHit_RequestHasStreamId, CacheFixture) {
  // Look up stream ID from cache
  std::string stream_id = cache[kStreamName];
  
  // Create request with stream ID from cache
  std::vector<std::shared_ptr<aws::kinesis::core::KinesisRecord>> items;
  auto context = std::make_shared<aws::kinesis::core::PutRecordsContext>(
      kStreamName, kStreamARN, stream_id, items);
  
  auto request = context->to_sdk_request();
  
  // Verify request has stream ID
  BOOST_CHECK_EQUAL(request.GetStreamId(), kStreamId);
}

// Test 2: Cache miss - request has no stream ID
BOOST_FIXTURE_TEST_CASE(CacheMiss_RequestHasNoStreamId, CacheFixture) {
  // Look up stream ID from cache (not found)
  auto it = cache.find("nonexistent-stream");
  std::string stream_id = (it != cache.end()) ? it->second : "";
  
  // Create request with empty stream ID
  std::vector<std::shared_ptr<aws::kinesis::core::KinesisRecord>> items;
  auto context = std::make_shared<aws::kinesis::core::PutRecordsContext>(
      "nonexistent-stream", kStreamARN, stream_id, items);
  
  auto request = context->to_sdk_request();
  
  // Verify request has no stream ID
  BOOST_CHECK(request.GetStreamId().empty());
}

// Test 3: Multiple streams - each request has correct stream ID
BOOST_FIXTURE_TEST_CASE(MultipleStreams_CorrectStreamIds, CacheFixture) {
  std::vector<std::shared_ptr<aws::kinesis::core::KinesisRecord>> items;
  
  // Test stream1
  auto context1 = std::make_shared<aws::kinesis::core::PutRecordsContext>(
      "stream1", kStreamARN, cache["stream1"], items);
  BOOST_CHECK_EQUAL(context1->to_sdk_request().GetStreamId(), "id1");
  
  // Test stream2
  auto context2 = std::make_shared<aws::kinesis::core::PutRecordsContext>(
      "stream2", kStreamARN, cache["stream2"], items);
  BOOST_CHECK_EQUAL(context2->to_sdk_request().GetStreamId(), "id2");
  
  // Test stream3
  auto context3 = std::make_shared<aws::kinesis::core::PutRecordsContext>(
      "stream3", kStreamARN, cache["stream3"], items);
  BOOST_CHECK_EQUAL(context3->to_sdk_request().GetStreamId(), "id3");
}

// Test 4: Real flow - lambda callback retrieves from cache
BOOST_FIXTURE_TEST_CASE(RealFlow_LambdaCallbackRetrievesFromCache, CacheFixture) {
  std::vector<std::shared_ptr<aws::kinesis::core::KinesisRecord>> items;
  
  // Use lambda to get stream ID (like Pipeline does)
  std::string stream_id = get_stream_id(kStreamName);
  
  // Verify lambda retrieved correct ID from cache
  BOOST_CHECK_EQUAL(stream_id, kStreamId);
  
  // Create request with stream ID from lambda
  auto context = std::make_shared<aws::kinesis::core::PutRecordsContext>(
      kStreamName, kStreamARN, stream_id, items);
  
  auto request = context->to_sdk_request();
  
  // Verify request has stream ID
  BOOST_CHECK_EQUAL(request.GetStreamId(), kStreamId);
  
  // Test cache miss through lambda
  std::string missing_id = get_stream_id("nonexistent");
  BOOST_CHECK(missing_id.empty());
}

BOOST_AUTO_TEST_SUITE_END()
