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

namespace aws {
namespace utils {

void set_log_level(const std::string& min_level) {
  auto min = boost::log::trivial::info;
  if (min_level == "warning") {
    min = boost::log::trivial::warning;
  } else if (min_level == "error") {
    min = boost::log::trivial::error;
  }
  boost::log::core::get()->set_filter(boost::log::trivial::severity >= min);
}

void setup_logging(const std::string& min_level) {
  set_log_level(min_level);

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
      << "[" << expr::format_date_time<boost::posix_time::ptime>(
            "TimeStamp",
            "%Y-%m-%d %H:%M:%S.%f") << "] "
      << "[" << expr::attr<boost::log::aux::thread::id>("ThreadID") << "] "
      << "[" << boost::log::trivial::severity << "] "
      << expr::smessage;
  sink->set_formatter(log_expr);

  boost::log::core::get()->add_sink(sink);
}

} //namespace utils
} //namespace aws
