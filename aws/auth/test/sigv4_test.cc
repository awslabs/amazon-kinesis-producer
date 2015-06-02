// Copyright 2010-2014 Amazon.com, Inc. or its affiliates. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License").
// You may not use this file except in compliance with the License.
// A copy of the License is located at
//
//  http://aws.amazon.com/apache2.0
//
// or in the "license" file accompanying this file. This file is distributed
// on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
// express or implied. See the License for the specific language governing
// permissions and limitations under the License.

#include <fstream>
#include <regex>
#include <set>
#include <sstream>

#include <boost/algorithm/string/trim.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/regex.hpp>
#include <boost/algorithm/string/case_conv.hpp>
#include <boost/filesystem.hpp>
#include <boost/test/unit_test.hpp>

#include <glog/logging.h>

#include <aws/http/http_request.h>
#include <aws/auth/sigv4.h>
#include <aws/utils/utils.h>

namespace {

const char* kService = "host";
const char* kRegion = "us-east-1";
const char* kAkid = "AKIDEXAMPLE";
const char* kSecretKey = "wJalrXUtnFEMI/K7MDENG+bPxRfiCYEXAMPLEKEY";

std::string read_file(const std::string& path) {
  std::ifstream t(path);
  std::stringstream buffer;
  buffer << t.rdbuf();
  return buffer.str();
}

// Unfortunately the joyent parser can't handle the weird cases in the test
// suite, so we're gonna have to roll our own.
aws::http::HttpRequest parse_request(std::string text) {
  std::vector<std::string> lines;
  boost::split_regex(lines, text, boost::regex("\r\n"));

  std::vector<std::string> first_line;
  boost::split(first_line,
               lines[0],
               boost::is_space(),
               boost::token_compress_on);

  BOOST_REQUIRE(first_line.size() >= 3);

  auto method = boost::trim_copy(first_line.front());
  auto path = boost::trim_copy(first_line[1]);
  auto http_version = boost::trim_copy(first_line.back());

  std::vector<std::pair<std::string, std::string>> headers;
  // Iterate from the 2nd line onwards until we hit the empty one that marks the
  // end of headers.
  auto last_header_it = lines.end();
  for (auto it = lines.begin() + 1; it != lines.end() && !it->empty(); ++it) {
    auto parts = aws::utils::split_on_first(*it, ":");
    if (parts.size() != 2) {
      throw std::runtime_error("Bad header");
    }
    headers.push_back(std::make_pair(parts[0], parts[1]));
    last_header_it = it;
  }

  std::string data;
  if ((last_header_it + 2) != lines.end()) {
    data = *(last_header_it + 2);
  }

  aws::http::HttpRequest req(method, path, http_version);
  if (!data.empty()) {
    req.set_no_content_length(true);
    req.set_data(data);
  }
  for (auto& h : headers) {
    req.add_header(h.first, h.second);
  }
  return req;
}

std::pair<std::string, std::string>
date_time_from_req(const aws::http::HttpRequest& req) {
  for (auto& h : req.headers()) {
    if (boost::to_lower_copy(h.first) == "date") {
      return aws::utils::get_date_time(
          aws::utils::parse_time(h.second, "%a, %d %b %Y %H:%M:%S GMT"));
    }
  }
  BOOST_FAIL("Did not find date header in request");
  return std::pair<std::string, std::string>();
}

class TestCredsProvider : public aws::auth::AwsCredentialsProvider {
 public:
  boost::optional<aws::auth::AwsCredentials> get_credentials() override {
    aws::auth::AwsCredentials c;
    c.akid = kAkid;
    c.secret_key = kSecretKey;
    return c;
  }
};

} //namespace

BOOST_AUTO_TEST_SUITE(SigV4)

// http://docs.aws.amazon.com/general/latest/gr/signature-v4-test-suite.html
BOOST_AUTO_TEST_CASE(Basic) {
  boost::filesystem::path dir("aws");
  dir = dir / "auth" / "test" / "aws4_testsuite";
  BOOST_REQUIRE(boost::filesystem::exists(dir));

  std::vector<boost::filesystem::path> paths;
  std::copy(boost::filesystem::directory_iterator(dir),
            boost::filesystem::directory_iterator(),
            std::back_inserter(paths));

  std::set<std::string> test_names;
  for (auto& p : paths) {
    test_names.insert(
        aws::utils::split_on_first(p.filename().string(), ".")[0]);
  }

  // The following test cases we are going to skip. They either involve a
  // malformed HTTP request, funky paths, or unescaped data. Our signer does not
  // perform escaping; it's up to the code creating the data to escape. For
  // Kinesis we use base-64, so we never have to do url escaping. For CloudWatch
  // we do it as we are generating data. As a client we never expect malformed
  // HTTP requests or funky paths because we're the ones creating them to begin
  // with. If and when we start supporting these extra stuff we can add the
  // tests back.
  for (auto s : { "get-header-value-multiline",
                  "post-vanilla-query-nonunreserved",
                  "post-vanilla-query-space",
                  "get-vanilla-ut8-query",
                  "get-slash-dot-slash",
                  "get-slash-pointless-dot",
                  "get-slashes",
                  "get-slash",
                  "get-relative-relative",
                  "get-relative" }) {
    test_names.erase(s);
  }

  for (auto& test_name : test_names) {
    auto req_file_path = dir / (test_name + ".req");
    auto req_text = read_file(req_file_path.string());
    aws::http::HttpRequest req;

    try {
      req = parse_request(req_text);
    } catch (const std::exception& e) {
      std::stringstream ss;
      ss << "The request for test case " << test_name
         << " could not be parsed. " << e.what();
      BOOST_FAIL(ss.str().c_str());
    }

    auto date_time = date_time_from_req(req);
    aws::auth::SigV4Context ctx(kRegion,
                                kService,
                                std::make_shared<TestCredsProvider>());
    aws::auth::RequestSigner signer(req, ctx, false, date_time);
    signer.sign();

    // For some reason there are carriage returns in all the test data files.
    // We have to remove them, except for sreq.
    auto load_file = [&](auto extension, bool remove_cr = true) {
      auto txt = read_file((dir / (test_name + extension)).string());
      if (remove_cr) {
        for (auto it = txt.begin(); it != txt.end();) {
          if (*it == '\r') {
            it = txt.erase(it);
          } else {
            it++;
          }
        }
      }
      return txt;
    };

    auto canon_req = load_file(".creq");
    auto str_to_sign = load_file(".sts");
    auto auth_header = load_file(".authz");
    auto signed_req = load_file(".sreq", false);

    // For some reason, in sreq, there is a space after Authorization:, but not
    // the other headers... we're going to remove it.
    std::string authz = "Authorization:";
    signed_req.erase(signed_req.find(authz) + authz.length(), 1);

    BOOST_CHECK_EQUAL(canon_req, signer.canon_request());
    BOOST_CHECK_EQUAL(str_to_sign, signer.str_to_sign());
    BOOST_CHECK_EQUAL(auth_header, signer.auth_header());
    BOOST_CHECK_EQUAL(signed_req, req.to_string());
  }
}

BOOST_AUTO_TEST_SUITE_END()
