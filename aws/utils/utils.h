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

#ifndef AWS_UTILS_UTILS_H_
#define AWS_UTILS_UTILS_H_

#include <atomic>
#include <chrono>
#include <vector>
#include <string>
#include <thread>
#include <memory>

#include <boost/random/random_device.hpp>
#include <boost/random/uniform_int_distribution.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/chrono/include.hpp>
#include <boost/thread.hpp>

// Grab bag of misc convenience functions
namespace aws {
namespace utils {

// Splits a string on the first instance of delimiter (and not others)
std::vector<std::string> split_on_first(const std::string& s,
                                        const std::string& delim);

// random printable string
std::string random_string(size_t len = 24);

uint64_t random_int(uint64_t min_inclusive, uint64_t max_exlusive);

// base16 encode
std::string hex(const unsigned char* hash, size_t len);

// returns raw bytes of the digest in a string
std::string md5(const std::string& data);

// returns raw bytes of the digest
std::array<uint8_t, 16> md5_binary(const std::string& data);

// digest converted to hex string
std::string md5_hex(const std::string& data);

// digest interpreted as a 128 bit big endian int and converted to decimal
std::string md5_decimal(const std::string& data);

std::pair<std::string, std::string> get_date_time(
    const boost::posix_time::ptime& pt =
        boost::posix_time::microsec_clock::universal_time());

boost::posix_time::ptime parse_time(
    const std::string& s,
    const std::string& format = "%Y-%m-%dT%H:%M:%SZ");

std::string format_ptime(
    const boost::posix_time::ptime& pt =
      boost::posix_time::microsec_clock::universal_time(),
    const std::string& format = "%Y-%m-%dT%H:%M:%SZ");

std::chrono::steady_clock::time_point
steady_tp_from_str(const std::string& date_time,
                   const std::string& format = "%Y-%m-%dT%H:%M:%SZ");

std::string url_encode(const std::string& s);

template <typename Precision, typename Clock = std::chrono::steady_clock>
typename Clock::time_point
round_down_time(typename Clock::time_point tp = Clock::now()) {
  auto d = std::chrono::duration_cast<Precision>(tp.time_since_epoch());
  return typename Clock::time_point(d);
}

uint64_t millis_since(const std::chrono::steady_clock::time_point& start);
uint64_t millis_till(const std::chrono::steady_clock::time_point& end);

template <typename Timepoint>
double seconds_between(Timepoint start, Timepoint end) {
  auto nanos =
      std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
  return (double) nanos / 1e9;
}

template <typename Timepoint>
double seconds_since(Timepoint start) {
  return seconds_between<Timepoint>(start, Timepoint::clock::now());
}

// Doesn't overwrite the item being examined
template <typename T>
bool compare_and_set(std::atomic<T>& a, const T& expected, const T& desired) {
  T tmp = expected;
  return a.compare_exchange_strong(tmp, desired);
}

template <typename Func, typename ...Params>
void checked_invoke(Func f, Params&&... params) {
  if (f(std::forward<Params>(params)...) != 1) {
    throw std::runtime_error("C function's return value indicated an error");
  }
}

template <typename Duration>
void sleep_for(Duration d) {
#if BOOST_OS_WINDOWS
  auto nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(d).count();
  boost::this_thread::sleep_for(boost::chrono::nanoseconds(nanos));
#else
  std::this_thread::sleep_for(d);
#endif
}

template <typename TimePoint>
void sleep_until(TimePoint t) {
#if BOOST_OS_WINDOWS
  auto nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(
      t - TimePoint::clock::now()).count();
  boost::this_thread::sleep_until(
      boost::chrono::high_resolution_clock::now() +
          boost::chrono::nanoseconds(nanos));
#else
  std::this_thread::sleep_until(t);
#endif
}

void enable_sdk_logging();

} // namespace utils
} // namespace aws

#endif
