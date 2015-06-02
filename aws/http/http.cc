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

#include <aws/http/http.h>

namespace aws {
namespace http {

HttpRequest create_kinesis_request(const std::string& region,
                                   const std::string& api_method,
                                   const std::string& data) {
  HttpRequest req(data.empty() ? "GET" : "POST", "/");
  req.add_header("Host", "kinesis." + region + ".amazonaws.com");
  req.add_header("Connection", "Keep-Alive");
  req.add_header("X-Amz-Target", "Kinesis_20131202." + api_method);
  req.add_header("User-Agent", "KinesisProducerLibrary_0.1");
  if (!data.empty()) {
    req.add_header("Content-Type", "application/x-amz-json-1.1");
  }
  req.set_data(data);
  return req;
}

HttpRequest create_kinesis_request(const std::string& region,
                                   const std::string& api_method) {
  return create_kinesis_request(region, api_method, "");
}

} // namespace http
} // namepsace aws
