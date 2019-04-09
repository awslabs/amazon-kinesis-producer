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

#ifndef AWS_UTILS_LOGGING_H_
#define AWS_UTILS_LOGGING_H_

#include <boost/filesystem.hpp>
#include <boost/log/sources/severity_logger.hpp>
#include <boost/log/trivial.hpp>
#include <aws/core/utils/logging/LogSystemInterface.h>


#define LOG(sev) BOOST_LOG_SEV(aws::utils::_logger, \
    boost::log::trivial::severity_level::sev) \
    << "[" << boost::filesystem::path(__FILE__).filename().string() \
    << ":" << __LINE__ << "] "

namespace aws {
namespace utils {

static boost::log::sources::severity_logger_mt<
    boost::log::trivial::severity_level> _logger;

boost::log::trivial::severity_level set_log_level(const std::string& min_level);

void setup_logging(const std::string& min_level = "info");
void setup_logging(boost::log::trivial::severity_level level);

void setup_aws_logging(Aws::Utils::Logging::LogLevel log_level);

} //namespace utils
} //namespace aws

#endif //AWS_UTILS_LOGGING_H_
