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

#ifndef AWS_UTILS_TIME_SENSITIVE_H_
#define AWS_UTILS_TIME_SENSITIVE_H_

#include <chrono>
#include <limits>

#include <boost/noncopyable.hpp>

namespace aws {
namespace utils {

class TimeSensitive : private boost::noncopyable {
 public:
  using Clock = std::chrono::steady_clock;
  using TimePoint = Clock::time_point;

  TimeSensitive() : arrival_(Clock::now()) {}

  TimeSensitive(TimePoint deadline, TimePoint expiration)
      : arrival_(Clock::now()),
        expiration_(expiration) {
    set_deadline(deadline);
  }

  TimePoint arrival() const noexcept {
    return arrival_;
  }

  TimePoint deadline() const noexcept {
    return deadline_;
  }

  TimePoint expiration() const noexcept {
    return expiration_;
  }

  void set_deadline(TimePoint tp) noexcept  {
    deadline_ = tp;
    if (expiration_.time_since_epoch().count() > 0 && expiration_ < deadline_) {
      deadline_ = expiration_;
    }
  }

  void set_expiration(TimePoint tp) noexcept {
    expiration_ = tp;
  }

  void set_deadline_from_now(std::chrono::milliseconds ms) {
    set_deadline(Clock::now() + ms);
  }

  void set_expiration_from_now(std::chrono::milliseconds ms) {
    expiration_ = Clock::now() + ms;
    if ((std::numeric_limits<int>::max() - Clock::now().time_since_epoch().count()) < ms.count()) {
      expiration_ = TimePoint::max();
    }
  }

  bool expired() const noexcept {
    return Clock::now() > expiration_;
  }

  void inherit_deadline_and_expiration(const TimeSensitive& other) {
    if (this->deadline().time_since_epoch().count() == 0 ||
        other.deadline() < this->deadline()) {
      this->set_deadline(other.deadline());
    }

    if (this->expiration().time_since_epoch().count() == 0 ||
        other.expiration() < this->expiration()) {
      this->set_expiration(other.expiration());
    }
  }

 private:
  TimePoint arrival_;
  TimePoint deadline_;
  TimePoint expiration_;
};

} //namespace utils
} //namespace aws

#endif //AWS_UTILS_TIME_SENSITIVE_H_
