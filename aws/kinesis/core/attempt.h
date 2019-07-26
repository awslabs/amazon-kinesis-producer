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

#ifndef AWS_KINESIS_CORE_ATTEMPT_H_
#define AWS_KINESIS_CORE_ATTEMPT_H_

#include <chrono>
#include <string>

namespace aws {
namespace kinesis {
namespace core {

class Attempt {
 public:
  using TimePoint = std::chrono::steady_clock::time_point;

  Attempt& set_start(TimePoint tp = std::chrono::steady_clock::now()) noexcept {
    start_ = tp;
    return *this;
  }

  Attempt& set_end(TimePoint tp = std::chrono::steady_clock::now()) noexcept {
    end_ = tp;
    return *this;
  }

  TimePoint start() const noexcept {
    return start_;
  }

  TimePoint end() const noexcept {
    return end_;
  }

  std::chrono::milliseconds duration() const noexcept {
    return std::chrono::duration_cast<std::chrono::milliseconds>(end_ - start_);
  }

  Attempt& set_error(const std::string& code, const std::string& msg) noexcept {
    error_ = true;
    error_code_ = code;
    error_message_ = msg;
    return *this;
  }

  Attempt& set_result(const std::string& shard_id,
                      const std::string& sequence_number) noexcept {
    error_ = false;
    shard_id_ = shard_id;
    sequence_number_ = sequence_number;
    return *this;
  }

  const std::string& shard_id() const noexcept {
    return shard_id_;
  }

  const std::string& sequence_number() const noexcept {
    return sequence_number_;
  }

  const std::string& error_code() const noexcept {
    return error_code_;
  }

  const std::string& error_message() const noexcept {
    return error_message_;
  }

  operator bool() const noexcept {
    return !error_;
  }

 private:
  TimePoint start_;
  TimePoint end_;
  bool error_;
  std::string error_code_;
  std::string error_message_;
  std::string sequence_number_;
  std::string shard_id_;
};

} //namespace core
} //namespace kinesis
} //namespace aws

#endif //AWS_KINESIS_CORE_ATTEMPT_H_
