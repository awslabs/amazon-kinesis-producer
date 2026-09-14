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

// Shared helpers for the Pipeline-level AUTO/strategy tests
// (pipeline_auto_test, strategy_pipeline_integration_test): a fixed-shard mock
// shard map and the deliberately-leaked Pipeline container. Both files need the
// same two pieces, so they live here rather than being copy-pasted.

#ifndef AWS_KINESIS_CORE_TEST_PIPELINE_TEST_SUPPORT_H_
#define AWS_KINESIS_CORE_TEST_PIPELINE_TEST_SUPPORT_H_

#include <memory>
#include <vector>

#include <boost/multiprecision/cpp_int.hpp>
#include <boost/optional.hpp>

#include <aws/kinesis/core/pipeline.h>
#include <aws/kinesis/core/shard_map.h>

namespace aws {
namespace kinesis {
namespace test {

// Fixed shard id the MockShardMap resolves every hash key to.
constexpr uint64_t kMockShardId = 7;

// Minimal ShardMap that always resolves a hash key to a fixed shard id, so a
// USER_PARTITION_KEY record always gets a predicted shard and aggregates.
class MockShardMap : public aws::kinesis::core::ShardMap {
 public:
  boost::optional<uint64_t> shard_id(
      const boost::multiprecision::uint128_t& hash_key) override {
    return kMockShardId;
  }
};

// Test pipelines are intentionally never destroyed, not even at process exit. A
// real Pipeline has no shutdown path: for AUTO/UNKNOWN streams the solo record
// flows into the limiter/collector, which schedule callbacks on the executor
// capturing the Pipeline. Destroying the Pipeline while such a callback is
// pending is a use-after-free that aborts. Production never destroys a Pipeline
// mid-flight, so we hold these in a deliberately-leaked container (allocated
// with new and never freed) rather than add a shutdown path solely for
// testability. Clean Pipeline shutdown is tracked separately, not part of this
// change.
inline std::vector<std::shared_ptr<aws::kinesis::core::Pipeline>>&
leaked_pipelines() {
  static auto* v =
      new std::vector<std::shared_ptr<aws::kinesis::core::Pipeline>>();
  return *v;
}

} //namespace test
} //namespace kinesis
} //namespace aws

#endif //AWS_KINESIS_CORE_TEST_PIPELINE_TEST_SUPPORT_H_
