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

#include <aws/metrics/metrics_index.h>

namespace aws {
namespace metrics {

std::shared_ptr<Metric>
MetricsIndex::get_metric(const MetricsFinder& metrics_finder) {
  assert(!metrics_finder.empty());

  {
    ReadLock lk(mutex_);
    auto it = metrics_.find(metrics_finder);
    if (it != metrics_.end()) {
      return it->second;
    }
  }

  WriteLock lk(mutex_);

  // check whether someone else beat us to it
  auto it = metrics_.find(metrics_finder);
  if (it != metrics_.end()) {
    return it->second;
  }

  std::vector<std::string> keys_to_add;
  std::vector<std::pair<std::string, std::string>> dims;
  std::shared_ptr<Metric> last_node;
  MetricsFinder mf(metrics_finder);

  while (!mf.empty() && it == metrics_.end()) {
    keys_to_add.push_back(mf);
    dims.push_back(mf.last_dimension());
    mf.pop_dimension();
    it = metrics_.find(mf);
  }

  if (it != metrics_.end()) {
    last_node = it->second;
  }

  assert(dims.size() == keys_to_add.size());

  for (int32_t i = dims.size() - 1; i >= 0; i--) {
    auto m = std::make_shared<Metric>(std::move(last_node), std::move(dims[i]));
    last_node = m;
    metrics_.emplace(std::piecewise_construct,
                     std::forward_as_tuple(std::move(keys_to_add[i])),
                     std::forward_as_tuple(std::move(m)));
  }

  return last_node;
}

std::vector<std::shared_ptr<Metric>> MetricsIndex::get_all() {
  std::vector<std::shared_ptr<Metric>> v;
  ReadLock lk(mutex_);
  for (auto& p : metrics_) {
    v.push_back(p.second);
  }
  return v;
}

} //namespace metrics
} //namespace aws
