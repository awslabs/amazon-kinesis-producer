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

#include <chrono>
#include <thread>

#include <boost/test/unit_test.hpp>

#include <aws/utils/logging.h>

#include <aws/utils/token_bucket.h>
#include <aws/mutex.h>
#include <aws/utils/utils.h>

BOOST_AUTO_TEST_SUITE(TokenBucket)

BOOST_AUTO_TEST_CASE(Basic) {
  const size_t max = 200;
  const size_t rate = 1000;

  auto start =
      std::chrono::steady_clock::time_point(
          std::chrono::duration_cast<std::chrono::milliseconds>(
              (std::chrono::steady_clock::now() + std::chrono::milliseconds(5))
                  .time_since_epoch()));
  aws::utils::sleep_until(start);

  aws::utils::TokenBucket b;
  b.add_token_stream(max, rate);

  BOOST_CHECK(!b.try_take({max + 1}));
  BOOST_CHECK(b.try_take({static_cast<double>(max)}));

  aws::utils::sleep_until(start + std::chrono::milliseconds(100));

  BOOST_CHECK(!b.try_take({110}));
  BOOST_CHECK(b.try_take({90}));

  aws::utils::sleep_until(start + std::chrono::milliseconds(max + 200));

  BOOST_CHECK(!b.try_take({max + 1}));
  BOOST_CHECK(b.try_take({static_cast<double>(max)}));
}

BOOST_AUTO_TEST_CASE(Multiple) {
  auto start =
      std::chrono::steady_clock::time_point(
          std::chrono::duration_cast<std::chrono::milliseconds>(
              (std::chrono::steady_clock::now() + std::chrono::milliseconds(5))
                  .time_since_epoch()));
  aws::utils::sleep_until(start);

  aws::utils::TokenBucket b;
  b.add_token_stream(200, 1000);
  b.add_token_stream(500, 2000);

  BOOST_CHECK(!b.try_take({201, 0}));
  BOOST_CHECK(!b.try_take({0, 501}));
  BOOST_CHECK(b.try_take({200, 500}));

  aws::utils::sleep_until(start + std::chrono::milliseconds(100));

  BOOST_CHECK(!b.try_take({110, 0}));
  BOOST_CHECK(!b.try_take({0, 220}));
  BOOST_CHECK(b.try_take({90, 180}));

  aws::utils::sleep_until(start + std::chrono::milliseconds(500));

  BOOST_CHECK(!b.try_take({201, 0}));
  BOOST_CHECK(!b.try_take({0, 501}));
  BOOST_CHECK(b.try_take({200, 500}));
}

BOOST_AUTO_TEST_SUITE_END()
