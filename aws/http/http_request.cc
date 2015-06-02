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

#include <aws/http/http_request.h>

#include <sstream>

#include <boost/algorithm/string/case_conv.hpp>

namespace aws {
namespace http {

HttpRequest::HttpRequest()
    : HttpMessage(false, HTTP_REQUEST) {
  settings_.on_url = [](auto parser, auto data, auto len) {
    auto m = static_cast<HttpRequest*>(parser->data);
    m->path_ = std::string(data, len);
    return 0;
  };
};

HttpRequest::HttpRequest(const std::string& method,
                         const std::string& path,
                         const std::string& http_version)
    : HttpMessage(true),
      method_(boost::to_upper_copy(method)),
      path_(path),
      http_version_(http_version) {};

void HttpRequest::on_message_complete(http_parser* parser) {
  switch (parser->method) {
    case HTTP_DELETE:
      method_ = "DELETE";
      break;
    case HTTP_GET:
      method_ = "GET";
      break;
    case HTTP_POST:
      method_ = "POST";
      break;
    case HTTP_HEAD:
      method_ = "HEAD";
      break;
    case HTTP_PUT:
      method_ = "PUT";
      break;
    default:
      method_ = "UNKNOWN";
  }
}

std::string HttpRequest::to_string() {
  generate_preamble();
  return preamble_ + data_;
}

void HttpRequest::generate_preamble() {
  std::stringstream ss;
  ss << method_ << " "
     << path_ << " "
     << http_version_
     << "\r\n";
  for (const auto& h : headers_) {
    ss << h.first << ":" << h.second << "\r\n";
  }
  ss << "\r\n";
  preamble_ = std::move(ss.str());
}

} //namespace http
} //namespace aws
