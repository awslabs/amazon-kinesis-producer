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

#include <aws/kinesis/core/stream_id_resolver.h>
#include <aws/kinesis/model/DescribeStreamSummaryRequest.h>
#include <aws/utils/logging.h>

namespace aws {
namespace kinesis {
namespace core {

std::string resolve_stream_id(
    const std::string& stream_name,
    std::shared_ptr<Configuration> config,
    std::shared_ptr<Aws::Kinesis::KinesisClient> kinesis_client) {
  
  // Priority 1: Check if customer provided StreamId manually
  std::string manual_stream_id = config->get_stream_id(stream_name);
  if (!manual_stream_id.empty()) {
    LOG(info) << "Using manual StreamId for stream: " << stream_name 
              << ", stream_id: " << manual_stream_id;
    return manual_stream_id;
  }
  
  // Priority 2: Check if auto-fetch is disabled
  if (!config->enable_stream_id_fetch()) {
    LOG(info) << "StreamId auto-fetch disabled for stream: " << stream_name;
    return "";
  }
  
  // Priority 3: Auto-fetch via DescribeStreamSummary
  LOG(info) << "Auto-fetching StreamId for stream: " << stream_name;
  
  const int max_retries = 3;
  int backoff_ms = 100;
  
  for (int attempt = 0; attempt < max_retries; attempt++) {
    Aws::Kinesis::Model::DescribeStreamSummaryRequest request;
    request.SetStreamName(stream_name);
    
    auto outcome = kinesis_client->DescribeStreamSummary(request);
    
    if (outcome.IsSuccess()) {
      const auto& summary = outcome.GetResult().GetStreamDescriptionSummary();
      
      // TODO: Use GetStreamId() when available in AWS SDK
      // For now, using StreamARN as placeholder
      std::string stream_id = summary.GetStreamId();
      LOG(info) << "Resolve stream id Auto-fetching StreamId for stream: " << stream_id;
      if (!stream_id.empty()) {
        LOG(info) << "Successfully fetched StreamId: " << stream_id;
        return stream_id;
      } else {
        LOG(info) << "StreamId not available in DescribeStreamSummary response";
        return "";
      }
    } else {
      auto error = outcome.GetError();
      std::string error_name = error.GetExceptionName();
      std::string error_message = error.GetMessage();
      
      // Non-retryable errors
      if (error_name == "ResourceNotFoundException" ||
          error_name == "AccessDeniedException" ||
          error_name == "InvalidArgumentException") {
        LOG(error) << "Non-retryable error fetching StreamId for stream: " 
                   << stream_name << ", error: " << error_name 
                   << ", message: " << error_message;
        return "";
      }
      
      // Retryable errors
      LOG(warning) << "Retryable error fetching StreamId (attempt " 
                   << (attempt + 1) << "/" << max_retries << "): " 
                   << error_name << ", message: " << error_message;
      
      if (attempt < max_retries - 1) {
        std::this_thread::sleep_for(std::chrono::milliseconds(backoff_ms));
        backoff_ms *= 2;  // Exponential backoff
      }
    }
  }
  
  LOG(error) << "Failed to fetch StreamId after " << max_retries 
             << " attempts for stream: " << stream_name;
  return "";
}

} //namespace core
} //namespace kinesis
} //namespace aws
