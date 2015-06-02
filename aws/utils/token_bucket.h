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

#ifndef AWS_UTILS_TOKEN_BUCKET_H_
#define AWS_UTILS_TOKEN_BUCKET_H_

#include <chrono>

#include <aws/utils/utils.h>

namespace aws {
namespace utils {

namespace detail {

class TokenStream {
 public:
  TokenStream(double max, double rate) noexcept
      : max_(max),
        rate_(rate) {}

  double tokens() noexcept {
    double nanos =
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            Clock::now() - last_).count();
    double growth = nanos == 0 ? 0 : (rate_ * nanos / 1e9);
    tokens_ = std::min(max_, tokens_ + growth);
    last_ = Clock::now();
    return tokens_;
  }

  void take(double n) noexcept {
    if (n > tokens()) {
      throw std::runtime_error("Not enough tokens");
    }
    tokens_ -= n;
  }

 private:
  using Clock = std::chrono::steady_clock;

  double max_;
  double rate_;
  Clock::time_point last_;
  double tokens_;
};

} //namespace detail

class TokenBucket {
 public:
  void add_token_stream(double max, double rate) {
    streams_.emplace_back(max, rate);
  }

  bool try_take(const std::initializer_list<double>& num_tokens) {
    if (!can_take(num_tokens)) {
      return false;
    }

    auto stream_it = streams_.begin();
    auto nt_it = num_tokens.begin();
    while (stream_it != streams_.end()) {
      stream_it->take(*nt_it);
      stream_it++;
      nt_it++;
    }

    return true;
  }

  bool can_take(const std::initializer_list<double>& num_tokens) {
    if (num_tokens.size() != streams_.size()) {
      throw std::runtime_error("Size of num_tokens list must be the same as "
                               "the number of token streams in the bucket");
    }

    auto stream_it = streams_.begin();
    auto nt_it = num_tokens.begin();
    while (stream_it != streams_.end()) {
      if (*nt_it > stream_it->tokens()) {
        return false;
      }
      stream_it++;
      nt_it++;
    }
    return true;
  }

 private:
  std::vector<detail::TokenStream> streams_;
};

} //namespace utils
} //namespace aws

#endif //AWS_UTILS_TOKEN_BUCKET_H_
