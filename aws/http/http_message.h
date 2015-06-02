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

#ifndef AWS_HTTP_HTTP_MESSAGE_H_
#define AWS_HTTP_HTTP_MESSAGE_H_

#include <memory>
#include <string>
#include <vector>

extern "C" {
  #include <aws/http/http-parser/http_parser.h>
}

namespace aws {
namespace http {

class HttpMessage {
 public:
  HttpMessage(bool no_parser, http_parser_type parse_type = HTTP_RESPONSE);

  void add_header(const std::string& name, const std::string& value);

  // Remove headers with the given name; case insensitive. Returns the number of
  // headers removed.
  size_t remove_headers(std::string name);

  void set_data(const std::string& s);

  void update(char* data, size_t len);

  virtual std::string to_string() = 0;

  const auto& headers() const {
    return headers_;
  }

  const std::string& data() const {
    return data_;
  }

  bool complete() const {
    return complete_;
  }

  void set_no_content_length(bool v) {
    no_content_length_ = v;
  }

 protected:
  virtual void on_message_complete(http_parser* parser) {};

  std::vector<std::pair<std::string, std::string>> headers_;
  std::string data_;
  std::unique_ptr<http_parser> parser_;
  http_parser_settings settings_;
  bool complete_;
  bool no_content_length_;
};

} //namespace http
} //namespace aws

#endif //AWS_HTTP_HTTP_MESSAGE_H_
