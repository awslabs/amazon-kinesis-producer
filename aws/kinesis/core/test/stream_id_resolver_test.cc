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

#include <aws/kinesis/core/stream_id_resolver.h>
#include <aws/kinesis/core/configuration.h>
#include <aws/kinesis/model/DescribeStreamSummaryRequest.h>
#include <aws/kinesis/model/DescribeStreamSummaryResult.h>
#include <aws/core/auth/AWSCredentialsProvider.h>

namespace {

const Aws::Auth::AWSCredentials kEmptyCreds("", "");

Aws::Client::ClientConfiguration fake_client_cfg() {
  Aws::Client::ClientConfiguration cfg;
  cfg.region = "us-west-1";
  cfg.endpointOverride = "localhost:61666";
  return cfg;
}

class MockKinesisClient : public Aws::Kinesis::KinesisClient {
 public:
  mutable bool describe_stream_summary_called = false;
  mutable std::string requested_stream_name;
  std::string mock_stream_id_to_return;
  bool should_fail = false;
  std::string error_code = "";

  MockKinesisClient(const std::string& stream_id = "mock-stream-id-12345")
      : Aws::Kinesis::KinesisClient(kEmptyCreds, fake_client_cfg()),
        mock_stream_id_to_return(stream_id) {}

  Aws::Kinesis::Model::DescribeStreamSummaryOutcome DescribeStreamSummary(
      const Aws::Kinesis::Model::DescribeStreamSummaryRequest& request) const override {
    describe_stream_summary_called = true;
    requested_stream_name = request.GetStreamName();

    if (should_fail) {
      return Aws::Kinesis::Model::DescribeStreamSummaryOutcome(
          Aws::Client::AWSError<Aws::Kinesis::KinesisErrors>(
              Aws::Kinesis::KinesisErrors::UNKNOWN,
              error_code,
              "Mock error message",
              false));
    }

    Aws::Kinesis::Model::DescribeStreamSummaryResult result;
    Aws::Kinesis::Model::StreamDescriptionSummary summary;
    summary.SetStreamName(request.GetStreamName());
    summary.SetStreamARN(mock_stream_id_to_return);  // Using ARN as placeholder for StreamId
    result.SetStreamDescriptionSummary(summary);
    return Aws::Kinesis::Model::DescribeStreamSummaryOutcome(result);
  }
};

std::shared_ptr<aws::kinesis::core::Configuration> make_config() {
  auto config = std::make_shared<aws::kinesis::core::Configuration>();
  return config;
}

} //namespace

BOOST_AUTO_TEST_SUITE(StreamIdResolver)

BOOST_AUTO_TEST_CASE(ManualStreamId_TakesPrecedence) {
  // Setup: Manual StreamId configured, auto-fetch enabled
  aws::kinesis::protobuf::Message msg;
  auto proto_config = msg.mutable_configuration();
  (*proto_config->mutable_stream_id_map())["test-stream"] = "manual-stream-id";
  proto_config->set_enable_stream_id_fetch(true);
  
  auto config = make_config();
  config->transfer_from_protobuf_msg(msg);
  
  auto mock_client = std::make_shared<MockKinesisClient>();
  
  // Execute
  std::string result = aws::kinesis::core::resolve_stream_id(
      "test-stream", config, mock_client);
  
  // Verify: Manual StreamId returned, no API call made
  BOOST_CHECK_EQUAL(result, "manual-stream-id");
  BOOST_CHECK(!mock_client->describe_stream_summary_called);
}

BOOST_AUTO_TEST_CASE(AutoFetch_WhenEnabled) {
  // Setup: No manual StreamId, auto-fetch enabled
  aws::kinesis::protobuf::Message msg;
  auto proto_config = msg.mutable_configuration();
  proto_config->set_enable_stream_id_fetch(true);
  
  auto config = make_config();
  config->transfer_from_protobuf_msg(msg);
  
  auto mock_client = std::make_shared<MockKinesisClient>("fetched-stream-id");
  
  // Execute
  std::string result = aws::kinesis::core::resolve_stream_id(
      "test-stream", config, mock_client);
  
  // Verify: API called and StreamId returned
  BOOST_CHECK(mock_client->describe_stream_summary_called);
  BOOST_CHECK_EQUAL(mock_client->requested_stream_name, "test-stream");
  BOOST_CHECK_EQUAL(result, "fetched-stream-id");
}

BOOST_AUTO_TEST_CASE(NoFetch_WhenDisabled) {
  // Setup: No manual StreamId, auto-fetch disabled
  aws::kinesis::protobuf::Message msg;
  auto proto_config = msg.mutable_configuration();
  proto_config->set_enable_stream_id_fetch(false);
  
  auto config = make_config();
  config->transfer_from_protobuf_msg(msg);
  
  auto mock_client = std::make_shared<MockKinesisClient>();
  
  // Execute
  std::string result = aws::kinesis::core::resolve_stream_id(
      "test-stream", config, mock_client);
  
  // Verify: No API call, empty string returned
  BOOST_CHECK(!mock_client->describe_stream_summary_called);
  BOOST_CHECK_EQUAL(result, "");
}

BOOST_AUTO_TEST_CASE(EmptyManualStreamId_FallsBackToAutoFetch) {
  // Setup: Empty manual StreamId, auto-fetch enabled
  aws::kinesis::protobuf::Message msg;
  auto proto_config = msg.mutable_configuration();
  (*proto_config->mutable_stream_id_map())["test-stream"] = "";
  proto_config->set_enable_stream_id_fetch(true);
  
  auto config = make_config();
  config->transfer_from_protobuf_msg(msg);
  
  auto mock_client = std::make_shared<MockKinesisClient>("auto-fetched-id");
  
  // Execute
  std::string result = aws::kinesis::core::resolve_stream_id(
      "test-stream", config, mock_client);
  
  // Verify: Falls back to auto-fetch
  BOOST_CHECK(mock_client->describe_stream_summary_called);
  BOOST_CHECK_EQUAL(result, "auto-fetched-id");
}

BOOST_AUTO_TEST_CASE(NonRetryableError_ReturnsEmpty) {
  // Setup: Auto-fetch enabled, but API returns non-retryable error
  aws::kinesis::protobuf::Message msg;
  auto proto_config = msg.mutable_configuration();
  proto_config->set_enable_stream_id_fetch(true);
  
  auto config = make_config();
  config->transfer_from_protobuf_msg(msg);
  
  auto mock_client = std::make_shared<MockKinesisClient>();
  mock_client->should_fail = true;
  mock_client->error_code = "ResourceNotFoundException";
  
  // Execute
  std::string result = aws::kinesis::core::resolve_stream_id(
      "test-stream", config, mock_client);
  
  // Verify: API called, empty string returned
  BOOST_CHECK(mock_client->describe_stream_summary_called);
  BOOST_CHECK_EQUAL(result, "");
}

BOOST_AUTO_TEST_SUITE_END()
