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

#ifndef AWS_METRICS_METRICS_INDEX_H_
#define AWS_METRICS_METRICS_INDEX_H_

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <boost/noncopyable.hpp>

#include <aws/mutex.h>
#include <aws/metrics/metrics_finder.h>
#include <aws/metrics/metric.h>

namespace aws {
namespace metrics {

class MetricsIndex : boost::noncopyable {
 public:
  std::shared_ptr<Metric> get_metric(const MetricsFinder& metrics_finder);

  std::vector<std::shared_ptr<Metric>> get_all();

 private:
  using Mutex = aws::shared_mutex;
  using ReadLock = aws::shared_lock<Mutex>;
  using WriteLock = aws::unique_lock<Mutex>;

  std::unordered_map<std::string, std::shared_ptr<Metric>> metrics_;
  Mutex mutex_;
};

} //namespace metrics
} //namespace aws

#endif //AWS_METRICS_METRICS_INDEX_H_
