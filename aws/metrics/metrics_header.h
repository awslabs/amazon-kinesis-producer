/*
 * Copyright 2025 Amazon.com, Inc. or its affiliates.
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

#ifndef AWS_METRICS_METRICS_HEADER_H
#define AWS_METRICS_METRICS_HEADER_H

#include <chrono>

namespace aws {
namespace metrics {

using Clock = std::chrono::steady_clock;
using TimePoint = Clock::time_point;

extern TimePoint upload_checkpoint_;

extern int test_global_int_;

} // namespace metrics
} // namespace aws

#endif //AWS_METRICS_METRICS_HEADER_H
