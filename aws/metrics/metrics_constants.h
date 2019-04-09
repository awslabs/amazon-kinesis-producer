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

#ifndef AWS_METRICS_METRICS_CONSTANTS_H_
#define AWS_METRICS_METRICS_CONSTANTS_H_

#include <list>
#include <string>
#include <unordered_map>
#include <vector>

#include <boost/assign/list_of.hpp>

#include <aws/utils/logging.h>

namespace aws {
namespace metrics {
namespace constants {

enum class Level {
  None = 0,
  Summary = 100,
  Detailed = 200
};

enum class Granularity {
  Global = 0,
  Stream = 100,
  Shard = 200
};

#define DEF_NAME(X) static constexpr const char* X = #X;
struct Names {
  DEF_NAME(Test);

  DEF_NAME(UserRecordsReceived);
  DEF_NAME(UserRecordsPending);
  DEF_NAME(UserRecordsPut);
  DEF_NAME(UserRecordsDataPut);

  DEF_NAME(KinesisRecordsPut);
  DEF_NAME(KinesisRecordsDataPut);

  DEF_NAME(ErrorsByCode);
  DEF_NAME(AllErrors);
  DEF_NAME(RetriesPerRecord);
  DEF_NAME(UserRecordExpired);

  DEF_NAME(BufferingTime);
  DEF_NAME(RequestTime);

  DEF_NAME(UserRecordsPerKinesisRecord);
  DEF_NAME(KinesisRecordsPerPutRecordsRequest);
  DEF_NAME(UserRecordsPerPutRecordsRequest);
};

struct Units {
  DEF_NAME(Count);
  DEF_NAME(Milliseconds);
  DEF_NAME(Bytes);
  DEF_NAME(None);
};

struct DimensionNames {
  DEF_NAME(MetricName);
  DEF_NAME(StreamName);
  DEF_NAME(ShardId);
  DEF_NAME(ErrorCode);
};
#undef DEF_NAME

bool filter(const std::vector<std::pair<std::string, std::string>>& dimensions,
            Level max_level,
            Granularity max_granularity);

std::string unit(const std::string& name);

Level level(const std::string& s);

Granularity granularity(const std::string& s);

} //namespace constants
} //namespace metrics
} //namespace aws

#endif //AWS_METRICS_METRICS_CONSTANTS_H_
