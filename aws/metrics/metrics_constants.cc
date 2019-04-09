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

#include <aws/metrics/metrics_constants.h>

namespace aws {
namespace metrics {
namespace constants {

bool filter(const std::vector<std::pair<std::string, std::string>>& dimensions,
            Level max_level,
            Granularity max_granularity) {
#define LEVEL(X, Y) (std::string(Names::X), Level::Y)
  static std::unordered_map<std::string, Level> level_map =
      boost::assign::map_list_of
          LEVEL( Test, Detailed )

          LEVEL( UserRecordsReceived, Detailed )
          LEVEL( UserRecordsPending, Detailed )
          LEVEL( UserRecordsPut, Summary )
          LEVEL( UserRecordsDataPut, Detailed )

          LEVEL( KinesisRecordsPut, Summary )
          LEVEL( KinesisRecordsDataPut, Detailed )

          LEVEL( ErrorsByCode, Summary )
          LEVEL( AllErrors, Summary )
          LEVEL( RetriesPerRecord, Detailed )
          LEVEL( UserRecordExpired, Summary )

          LEVEL( BufferingTime, Summary )
          LEVEL( RequestTime, Detailed )

          LEVEL( UserRecordsPerKinesisRecord, Detailed )
          LEVEL( KinesisRecordsPerPutRecordsRequest, Detailed )
          LEVEL( UserRecordsPerPutRecordsRequest, Detailed );
#undef LEVEL

  if (max_level == Level::None) {
    return false;
  }

  assert(!dimensions.empty());
  assert(dimensions.front().first == "MetricName");

  auto level = Level::Detailed;
  auto it = level_map.find(dimensions.front().second);
  if (it != level_map.end()) {
    level = it->second;
  } else {
    LOG(warning) << "Unknown metric \"" << dimensions.front().second
                 << "\", did you forget to add it to "
                 << "aws::metrics::constants::filter()?";
  }

  auto granularity = Granularity::Global;
  for (auto& p : dimensions) {
    if (p.first == "StreamName") {
      granularity = Granularity::Stream;
    } else if (p.first == "ShardId") {
      granularity = Granularity::Shard;
    }
  }

  if (level > max_level || granularity > max_granularity) {
    return false;
  }

  return true;
}

std::string unit(const std::string& name) {
#define UNIT(X, Y) (std::string(Names::X), std::string(Units::Y))
  static std::unordered_map<std::string, std::string> unit_map =
      boost::assign::map_list_of
          UNIT( Test, Count )

          UNIT( UserRecordsReceived, Count )
          UNIT( UserRecordsPending, Count )
          UNIT( UserRecordsPut, Count )
          UNIT( UserRecordsDataPut, Bytes )

          UNIT( KinesisRecordsPut, Count )
          UNIT( KinesisRecordsDataPut, Bytes )

          UNIT( ErrorsByCode, Count )
          UNIT( AllErrors, Count )
          UNIT( RetriesPerRecord, Count )
          UNIT( UserRecordExpired, Count )

          UNIT( BufferingTime, Milliseconds )
          UNIT( RequestTime, Milliseconds )

          UNIT( UserRecordsPerKinesisRecord, Count )
          UNIT( KinesisRecordsPerPutRecordsRequest, Count )
          UNIT( UserRecordsPerPutRecordsRequest, Count );
#undef UNIT

  auto it = unit_map.find(name);
  if (it != unit_map.end()) {
    return it->second;
  }
  LOG(warning) << "Unknown metric \"" << name << "\", did you forget to add it "
               << "to aws::metrics::constants::unit()?";
  return Units::None;
}

Level level(const std::string& s) {
  if (s == "none") {
    return Level::None;
  } else if (s == "summary") {
    return Level::Summary;
  } else if (s == "detailed") {
    return Level::Detailed;
  } else {
    throw std::runtime_error("Unknown metric level " + s);
  }
}

Granularity granularity(const std::string& s) {
  if (s == "global") {
    return Granularity::Global;
  } else if (s == "stream") {
    return Granularity::Stream;
  } else if (s == "shard") {
    return Granularity::Shard;
  } else {
    throw std::runtime_error("Unknown metric granularity " + s);
  }
}

} //namespace constants
} //namespace metrics
} //namespace aws
