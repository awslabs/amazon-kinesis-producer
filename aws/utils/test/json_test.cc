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

#include <boost/test/unit_test.hpp>

#include <aws/utils/logging.h>

#include <aws/utils/json.h>

BOOST_AUTO_TEST_SUITE(Json)

BOOST_AUTO_TEST_CASE(BadJson) {
  std::string s = R"(
  {
    "foo" : ["bar", "baz"],
    "x" : "y"
  )";

  try {
    aws::utils::Json j(s);
    BOOST_FAIL("Should've thrown an exception from bad json");
  } catch (const std::exception& e) {
    // ok
  }
}

BOOST_AUTO_TEST_CASE(String) {
  std::string s = R"(
  {
    "x" : "y",
    "a" : "b"
  }
  )";

  aws::utils::Json j(s);
  BOOST_CHECK_EQUAL((std::string) j["x"], "y");
  BOOST_CHECK_EQUAL((std::string) j["a"], "b");

  try {
    std::string s = j["q"];
    BOOST_FAIL("Should've thrown an exception trying to convert a null json "
               "object to string");
    (void) s;
  } catch (const std::exception& e) {
    // ok
  }
}

BOOST_AUTO_TEST_CASE(Nested) {
  std::string s = R"(
  {
    "x" : { "y" : "z" },
    "a" : "b"
  }
  )";

  aws::utils::Json j(s);
  BOOST_CHECK_EQUAL((std::string) j["x"]["y"], "z");
  BOOST_CHECK_EQUAL((std::string) j["a"], "b");
}

BOOST_AUTO_TEST_CASE(Array) {
  std::string s = R"(
  {
    "x" : [ "a", "b" ],
    "y" : [ { "k1" : "v1" }, { "k2" : "v2" }]
  }
  )";

  aws::utils::Json j(s);
  BOOST_CHECK_EQUAL(j["x"].size(), 2);
  BOOST_CHECK_EQUAL((std::string) j["x"][0], "a");
  BOOST_CHECK_EQUAL((std::string) j["x"][1], "b");
  BOOST_CHECK(!(bool) j["x"][2]);
  BOOST_CHECK_EQUAL((std::string) j["y"][0]["k1"], "v1");
  BOOST_CHECK_EQUAL((std::string) j["y"][1]["k2"], "v2");
}

BOOST_AUTO_TEST_SUITE_END()
