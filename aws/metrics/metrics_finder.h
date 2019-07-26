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

#ifndef AWS_METRICS_METRICS_FINDER_H_
#define AWS_METRICS_METRICS_FINDER_H_

namespace aws {
namespace metrics {

class MetricsFinder {
 public:
  MetricsFinder() = default;
  MetricsFinder(const MetricsFinder&) = default;
  MetricsFinder(MetricsFinder&&) = default;
  MetricsFinder& operator =(const MetricsFinder&) = default;
  MetricsFinder& operator =(MetricsFinder&&) = default;

  MetricsFinder& push_dimension(std::string k, std::string v) noexcept {
    delims_.emplace_back(canon_.size(), k.length());
    canon_ += (char) 0;
    canon_ += k;
    canon_ += (char) 1;
    canon_ += v;
    return *this;
  }

  MetricsFinder& pop_dimension() noexcept {
    if (!delims_.empty()) {
      canon_.erase(delims_.back().first);
      delims_.pop_back();
    }
    return *this;
  }

  std::pair<std::string, std::string> last_dimension() const noexcept {
    if (empty()) {
      throw std::runtime_error(
          "Cannot call last_dimension() on a MetricsFinder that's empty");
    }

    auto& d = delims_.back();
    return std::make_pair(canon_.substr(d.first + 1, d.second),
                          canon_.substr(d.first + 1 + d.second + 1));
  }

  operator const std::string&() const noexcept {
    return canon_;
  }

  bool empty() const noexcept {
    return delims_.empty();
  }

 private:
  std::string canon_;
  std::vector<std::pair<size_t, size_t>> delims_;
};

} //namespace metrics
} //namespace aws

#endif //AWS_METRICS_METRICS_FINDER_H_
