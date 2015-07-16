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

#ifndef AWS_UTILS_LOGGING_H_
#define AWS_UTILS_LOGGING_H_

#include <boost/filesystem.hpp>
#include <boost/log/sources/severity_logger.hpp>
#include <boost/log/trivial.hpp>

#define LOG(sev) BOOST_LOG_SEV(aws::utils::_logger, \
    boost::log::trivial::severity_level::sev) \
    << "[" << boost::filesystem::path(__FILE__).filename().string() \
    << ":" << __LINE__ << "] "

namespace aws {
namespace utils {

static boost::log::sources::severity_logger_mt<
    boost::log::trivial::severity_level> _logger;

void set_log_level(const std::string& min_level);

void setup_logging(const std::string& min_level = "info");

} //namespace utils
} //namespace aws

#endif //AWS_UTILS_LOGGING_H_
