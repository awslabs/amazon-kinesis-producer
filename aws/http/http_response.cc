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

#include <aws/http/http_response.h>

#include <sstream>

namespace aws {
namespace http {

HttpResponse::HttpResponse()
    : HttpMessage(false, HTTP_RESPONSE) {};

HttpResponse::HttpResponse(int code)
    : HttpMessage(true),
      status_code_(code) {};

void HttpResponse::on_message_complete(http_parser* parser) {
  status_code_ = parser->status_code;
}

std::string HttpResponse::to_string() {
  std::stringstream ss;
  ss << "HTTP/1.1 "
     << status_code_ << " "
     << status_string(status_code_)
     << "\r\n";
  for (const auto& h : headers_) {
    ss << h.first << ": " << h.second << "\r\n";
  }
  ss << "\r\n";
  ss << data_;
  return std::move(ss.str());
}

const char* HttpResponse::status_string(int code) {
  switch (code) {
    case 200:
      return "OK";
    case 400:
      return "BAD REQUEST";
    case 401:
      return "UNAUTHORIZED";
    case 403:
      return "FORBIDDEN";
    case 404:
      return "NOT FOUND";
    case 500:
      return "INTERNAL SERVER ERROR";
    case 501:
      return "NOT IMPLEMENTED";
    case 503:
      return "SERVICE UNAVAILABLE";
    case 504:
      return "GATEWAY TIMEOUT";
    default:
      return "UNKNOWN";
  }
}

} //namespace http
} //namespace aws
