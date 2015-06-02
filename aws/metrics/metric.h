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

#ifndef AWS_METRICS_METRIC_H_
#define AWS_METRICS_METRIC_H_

#include <list>
#include <memory>
#include <string>
#include <vector>

#include <aws/metrics/accumulator.h>

namespace aws {
namespace metrics {

class Metric {
 public:
  using Dimension = std::pair<std::string, std::string>;

  Metric(std::shared_ptr<Metric> parent, Dimension d)
      : parent_(std::move(parent)),
        dimension_(std::move(d)),
        accumulator_(std::make_shared<Accumulator>()) {
    if (parent_) {
      all_dimensions_.insert(all_dimensions_.end(),
                             parent_->all_dimensions().cbegin(),
                             parent_->all_dimensions().cend());
    }
    all_dimensions_.push_back(dimension_);
  }

  Accumulator& accumulator() const noexcept {
    return *accumulator_;
  }

  const Dimension& dimension() const noexcept {
    return dimension_;
  }

  const std::vector<Dimension>& all_dimensions() const noexcept {
      return all_dimensions_;
  }

  const std::shared_ptr<Metric>& parent() const noexcept {
    return parent_;
  }

  void put(double val) {
    accumulator_->put(val);
    if (parent_) {
      parent_->put(val);
    }
  }

 private:
  std::shared_ptr<Metric> parent_;
  Dimension dimension_;
  std::vector<Dimension> all_dimensions_;
  std::shared_ptr<Accumulator> accumulator_;
};

} //namespace metrics
} //namespace aws

#endif //AWS_METRICS_METRIC_H_
