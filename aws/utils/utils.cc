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

#include <aws/utils/utils.h>

#include <array>
#include <iomanip>
#include <iostream>

#include <boost/algorithm/string/case_conv.hpp>
#include <boost/algorithm/string/trim.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/replace.hpp>
#include <boost/algorithm/string/find.hpp>
#include <boost/multiprecision/cpp_int.hpp>
#include <boost/date_time.hpp>


#include <aws/core/utils/logging/DefaultLogSystem.h>
#include <aws/core/utils/logging/AWSLogging.h>
#include <aws/core/utils/logging/LogLevel.h>
#include <aws/utils/logging.h>

#include <openssl/md5.h>

namespace aws {
namespace utils {

std::vector<std::string> split_on_first(const std::string& s,
                                        const std::string& delim) {
  auto it_range = boost::find_first(s, delim);
  if (!it_range) {
    return {s};
  } else {
    return {
      std::string(s.begin(), it_range.begin()),
      std::string(it_range.end(), s.end())
    };
  }
}

std::string random_string(size_t len) {
  static boost::random::random_device rng;
  static boost::random::uniform_int_distribution<> dist(32, 126);
  std::stringstream ss;
  for(size_t i = 0; i < len; ++i) {
    ss << (char) dist(rng);
  }
  return ss.str();
}

uint64_t random_int(uint64_t min_inclusive, uint64_t max_exlusive) {
  static boost::random::random_device rng;
  static boost::random::uniform_int_distribution<uint64_t> dist(
      0,
      0xFFFFFFFFFFFFFFFF);
  return min_inclusive + (dist(rng) % (max_exlusive - min_inclusive));
}

std::string hex(const unsigned char* hash, size_t len) {
  std::stringstream ss;
  ss << std::hex << std::setfill('0');
  for (size_t i = 0; i < len; ++i) {
    ss << std::hex << std::setw(2) << (unsigned int) hash[i];
  }
  return ss.str();
}

std::string md5(const std::string& data) {
  return std::string((char*) md5_binary(data).data(), MD5_DIGEST_LENGTH);
}

std::array<uint8_t, 16> md5_binary(const std::string& data) {
  MD5_CTX ctx;
  checked_invoke(&MD5_Init, &ctx);
  checked_invoke(&MD5_Update, &ctx, data.data(), data.length());
  std::array<uint8_t, MD5_DIGEST_LENGTH> buf;
  checked_invoke(&MD5_Final, (unsigned char*) buf.data(), &ctx);
  return buf;
}

std::string md5_hex(const std::string& data) {
  auto bytes = md5(data);
  return hex((const unsigned char*) bytes.data(), bytes.length());
}

std::string md5_decimal(const std::string& data) {
  auto hash = aws::utils::md5_hex(data);
  std::stringstream ss;
  ss << boost::multiprecision::uint128_t("0x" + hash);
  return ss.str();
}

std::pair<std::string, std::string>
get_date_time(const boost::posix_time::ptime& pt) {
  return std::make_pair(format_ptime(pt, "%Y%m%d"), format_ptime(pt, "%H%M%S"));
}

boost::posix_time::ptime parse_time(const std::string& s,
                                    const std::string& format) {
  // the stream will destroy the facet
  auto facet = new boost::posix_time::time_input_facet();
  facet->format(format.c_str());
  std::stringstream ss(s);
  ss.imbue(std::locale(ss.getloc(), facet));
  boost::posix_time::ptime pt;
  ss >> pt;
  return pt;
}

std::string format_ptime(const boost::posix_time::ptime& pt,
                         const std::string& format) {
  // the stream will destroy the facet
  auto facet = new boost::posix_time::time_facet();
  facet->format(format.c_str());
  std::stringstream ss;
  ss.imbue(std::locale(ss.getloc(), facet));
  ss << pt;
  return ss.str();
}

std::chrono::steady_clock::time_point
steady_tp_from_str(const std::string& date_time, const std::string& format) {
  auto pt = parse_time(date_time, format);
  auto from_now = pt - boost::posix_time::microsec_clock::universal_time();
  return std::chrono::steady_clock::now() +
      std::chrono::nanoseconds(from_now.total_nanoseconds());
}

std::string url_encode(const std::string& s) {
  static const std::string hex_alphabet = "0123456789ABCDEF";
  std::string r;
  for (auto c : s) {
    bool is_digit = c >= 48 && c <= 57;
    bool is_alphabet = (c >= 65 && c <= 90) || (c >= 97 && c <= 122);
    bool is_unreserved = c == '-' || c == '~' || c == '_' || c == '.';
    if (is_digit || is_alphabet || is_unreserved) {
      r += c;
    } else {
      r += '%';
      r += hex_alphabet[c / 16];
      r += hex_alphabet[c % 16];
    }
  }
  return r;
}

uint64_t millis_since(const std::chrono::steady_clock::time_point& start) {
  auto now = std::chrono::steady_clock::now();
  if (start >= now) {
    return 0;
  } else {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        now - start).count();
  }
}

uint64_t millis_till(const std::chrono::steady_clock::time_point& end) {
  auto now = std::chrono::steady_clock::now();
  if (end <= now) {
    return 0;
  } else {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        end - now).count();
  }
}

void enable_sdk_logging() {
  Aws::Utils::Logging::InitializeAWSLogging(
      Aws::MakeShared<Aws::Utils::Logging::DefaultLogSystem>(
          "log",
          Aws::Utils::Logging::LogLevel::Trace,
          "aws_sdk_"));
}

} // namespace utils
} // namespace aws
