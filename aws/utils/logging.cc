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

#include <aws/utils/logging.h>

#include <boost/core/null_deleter.hpp>
#include <boost/date_time/posix_time/posix_time_types.hpp>
#include <boost/log/core.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/sinks/text_ostream_backend.hpp>
#include <boost/log/sinks/sync_frontend.hpp>
#include <boost/log/sources/record_ostream.hpp>
#include <boost/log/support/date_time.hpp>
#include <boost/log/utility/setup/common_attributes.hpp>
#include <iostream>
#include <mutex>
#include <cstdint>
#include <cstdarg>
#include <stdio.h>

#include <aws/core/utils/memory/stl/AWSString.h>
#include <aws/core/utils/logging/AWSLogging.h>
#include <aws/core/utils/Array.h>
#include <aws/core/utils/logging/LogLevel.h>

namespace aws {
namespace utils {

using BoostLevel = boost::log::trivial::severity_level;

BoostLevel set_log_level(const std::string& min_level) {
  auto min = boost::log::trivial::info;
  if (min_level == "warning") {
    min = boost::log::trivial::warning;
  } else if (min_level == "error") {
    min = boost::log::trivial::error;
  }
  return min;
}

void setup_logging(const std::string& min_level) {
  auto level = set_log_level(min_level);
  setup_logging(level);
}

void setup_logging(const BoostLevel level) {
    boost::log::core::get()->set_filter(boost::log::trivial::severity >= level);
    using OstreamBackend = boost::log::sinks::text_ostream_backend;
    auto backend = boost::make_shared<OstreamBackend>();
    backend->add_stream(
        boost::shared_ptr<std::ostream>(
            &std::cerr,
            boost::null_deleter()));
    auto sink =
        boost::make_shared<boost::log::sinks::synchronous_sink<OstreamBackend>>(
            backend);

    boost::log::add_common_attributes();
    namespace expr = boost::log::expressions;
    auto log_expr = expr::stream
        << "++++" << std::endl
        << "[" << expr::format_date_time<boost::posix_time::ptime>(
              "TimeStamp",
              "%Y-%m-%d %H:%M:%S.%f") << "] "
        << "[" << expr::attr<boost::log::attributes::current_process_id::value_type>("ProcessID") << "]"
        << "[" << expr::attr<boost::log::attributes::current_thread_id::value_type>("ThreadID") << "] "
        << "[" << boost::log::trivial::severity << "] "
        << expr::smessage
        << std::endl << "----";
    sink->set_formatter(log_expr);

    boost::log::core::get()->add_sink(sink);
    LOG(info) << "Set boost log level to " << boost::log::trivial::to_string(level);
}
using BoostLog = boost::log::trivial::severity_level;
const BoostLog AwsLevelToBoostLevel[] = {
  BoostLog::fatal,
  BoostLog::fatal,
  BoostLog::error,
  BoostLog::warning,
  BoostLog::info,
  BoostLog::debug,
  BoostLog::trace
};

using namespace Aws::Utils::Logging;

class AwsBoostLogInterface : public Aws::Utils::Logging::LogSystemInterface {

private:
  LogLevel logLevel_;

public:

  AwsBoostLogInterface(LogLevel logLevel) : logLevel_(logLevel) {
  }

  virtual LogLevel GetLogLevel(void) const override {
    return logLevel_;
  }

  virtual void Log(LogLevel logLevel, const char* tag, const char* formatStr, ...) override {
    using namespace Aws::Utils;
    //
    // Borrowed from AWS SDK aws/core/utils/logging/FormattedLogSystem.cpp
    //
    Aws::StringStream ss;

    std::va_list args;
    va_start(args, formatStr);

    va_list tmp_args; //unfortunately you cannot consume a va_list twice
    va_copy(tmp_args, args); //so we have to copy it
    #ifdef WIN32
        const int requiredLength = _vscprintf(formatStr, tmp_args) + 1;
    #else
        const int requiredLength = vsnprintf(nullptr, 0, formatStr, tmp_args) + 1;
    #endif
    va_end(tmp_args);

    Array<char> outputBuff(requiredLength);
    #ifdef WIN32
        vsnprintf_s(outputBuff.GetUnderlyingData(), requiredLength, _TRUNCATE, formatStr, args);
    #else
        vsnprintf(outputBuff.GetUnderlyingData(), requiredLength, formatStr, args);
    #endif // WIN32

    ss << outputBuff.GetUnderlyingData() << std::endl;

    LogToBoost(logLevel, tag, ss.str());

    va_end(args);
  }

  virtual void LogStream(LogLevel logLevel, const char* tag, const Aws::OStringStream &messageStream) override {
    LogToBoost(logLevel, tag, messageStream.str());
  }

  void LogToBoost(LogLevel logLevel, const char* tag, const std::string& message) {
    BoostLog level = BoostLog::error;
    int logLevelInt = static_cast<int>(logLevel);
    if (logLevelInt < 0) {
      LOG(error) << "Aws::Utils::Logging::LogLevel value less than 0: '" << logLevelInt << "'.  Defaulting to error";
    } else {
      size_t logLevelSize = static_cast<size_t>(logLevelInt);
      if (logLevelSize >= sizeof(AwsLevelToBoostLevel)) {
        LOG(error) << "Aws::Utils::Logging::LogLevel value not found in mapping: '" << logLevelInt << "'.  Defaulting to error";
      } else {
        level = AwsLevelToBoostLevel[logLevelSize];
      }
    }
    BOOST_LOG_SEV(aws::utils::_logger, level) << "[AWS Log: " << Aws::Utils::Logging::GetLogLevelName(logLevel) << "](" << tag << ")" << message;
  }
};

void setup_aws_logging(Aws::Utils::Logging::LogLevel log_level) {
    Aws::Utils::Logging::InitializeAWSLogging(
      Aws::MakeShared<AwsBoostLogInterface>("kpl-sdk-logging", log_level));

    LOG(info) << "Set AWS Log Level to " << Aws::Utils::Logging::GetLogLevelName(log_level);
}

} //namespace utils
} //namespace aws
