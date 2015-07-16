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

#ifndef AWS_HTTP_HTTP_H_
#define AWS_HTTP_HTTP_H_

#include <aws/http/http_request.h>
#include <aws/http/http_response.h>

namespace aws {
namespace http {

HttpRequest create_kinesis_request(const std::string& region,
                                   const std::string& api_method,
                                   const std::string& data = "");

} // namespace http
} // namespace aws

#endif // AWS_HTTP_HTTP_H_
