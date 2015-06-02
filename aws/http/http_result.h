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

#ifndef AWS_HTTP_HTTP_RESULT_H_
#define AWS_HTTP_HTTP_RESULT_H_

#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include <boost/optional.hpp>

#include <aws/http/http_response.h>

namespace aws {
namespace http {

class HttpResult {
 public:
  using TimePoint = std::chrono::steady_clock::time_point;

  HttpResult(std::unique_ptr<HttpResponse>&& response,
             std::shared_ptr<void> context = std::shared_ptr<void>(),
             TimePoint start_time = std::chrono::steady_clock::now(),
             TimePoint end_time = std::chrono::steady_clock::now())
      : response_(std::forward<std::unique_ptr<HttpResponse>>(response)),
        context_(std::move(context)),
        start_time_(start_time),
        end_time_(end_time) {}

  HttpResult(std::string&& error,
             std::shared_ptr<void> context = std::shared_ptr<void>(),
             TimePoint start_time = std::chrono::steady_clock::now(),
             TimePoint end_time = std::chrono::steady_clock::now())
      : error_(std::forward<std::string>(error)),
        context_(std::move(context)),
        start_time_(start_time),
        end_time_(end_time) {}

  bool successful() const noexcept {
    return !error_;
  }

  std::string error() const noexcept {
    if (error_) {
      return *error_;
    } else {
      return "";
    }
  }

  const std::string& response_body() const {
    return response_->data();
  }

  int status_code() const noexcept {
    return response_ ? response_->status_code() : -1;
  }

  template <typename T>
  std::shared_ptr<T> context() const {
    return std::static_pointer_cast<T>(context_);
  }

  TimePoint start_time() const {
    return start_time_;
  }

  TimePoint end_time() const {
    return end_time_;
  }

  uint64_t duration_millis() const {
    return
        std::chrono::duration_cast<std::chrono::milliseconds>(
            end_time() - start_time()).count();
  }

  uint64_t duration_micros() const {
    return
        std::chrono::duration_cast<std::chrono::microseconds>(
            end_time() - start_time()).count();
  }

  const std::vector<std::pair<std::string, std::string>>& headers() {
    return response_->headers();
  }

  operator bool() {
    return successful();
  }

 private:
  boost::optional<std::string> error_;
  std::unique_ptr<HttpResponse> response_;
  std::shared_ptr<void> context_;
  TimePoint start_time_;
  TimePoint end_time_;
};

} //namespace http
} //namespace aws

#endif //AWS_HTTP_HTTP_RESULT_H_
