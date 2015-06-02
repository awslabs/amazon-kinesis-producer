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

#ifndef AWS_HTTP_HTTP_REQUEST_H_
#define AWS_HTTP_HTTP_REQUEST_H_

#include <aws/http/http_message.h>

namespace aws {
namespace http {

class HttpRequest : public HttpMessage {
 public:
  HttpRequest();

  HttpRequest(const std::string& method,
              const std::string& path,
              const std::string& http_version = "HTTP/1.1");

  std::string to_string() override;

  const std::string& method() const {
    return method_;
  }

  const std::string& path() const {
    return path_;
  }

  operator std::string() {
    return to_string();
  }

 protected:
  void generate_preamble();
  void on_message_complete(http_parser* parser) override;

 private:
  std::string preamble_;
  std::string method_;
  std::string path_;
  std::string http_version_;
};

} //namespace http
} //namespace aws

#endif //AWS_HTTP_HTTP_REQUEST_H_
