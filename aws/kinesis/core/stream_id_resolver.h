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

#ifndef AWS_KINESIS_CORE_STREAM_ID_RESOLVER_H_
#define AWS_KINESIS_CORE_STREAM_ID_RESOLVER_H_

#include <memory>
#include <string>

#include <aws/kinesis/KinesisClient.h>
#include <aws/kinesis/core/configuration.h>

namespace aws {
namespace kinesis {
namespace core {

/**
 * Utility for resolving StreamId based on configuration.
 * 
 * Priority order:
 * 1. Manual StreamId from configuration (stream_id_map)
 * 2. Auto-fetch via DescribeStreamSummary (if enable_stream_id_fetch is true)
 * 3. Empty string (if auto-fetch is disabled)
 */
std::string resolve_stream_id(
    const std::string& stream_name,
    std::shared_ptr<Configuration> config,
    std::shared_ptr<Aws::Kinesis::KinesisClient> kinesis_client);

} //namespace core
} //namespace kinesis
} //namespace aws

#endif //AWS_KINESIS_CORE_STREAM_ID_RESOLVER_H_
