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

#ifndef AWS_HTTP_HTTP_RESPONSE_H_
#define AWS_HTTP_HTTP_RESPONSE_H_

#include <aws/http/http_message.h>

namespace aws {
namespace http {

class HttpResponse : public HttpMessage {
 public:
  HttpResponse();

  HttpResponse(int code);

  std::string to_string() override;

  int status_code() const {
    return status_code_;
  }

 protected:
  void on_message_complete(http_parser* parser) override;

 private:
  static const char* status_string(int code);

  int status_code_;
};


} //namespace http
} //namespace aws

#endif //AWS_HTTP_HTTP_RESPONSE_H_
