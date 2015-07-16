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

#include <aws/http/ec2_metadata.h>
#include <aws/kinesis/test/test_tls_server.h>
#include <aws/http/io_service_socket.h>
#include <aws/utils/io_service_executor.h>
#include <aws/utils/utils.h>

namespace {

const int kPort = aws::kinesis::test::TestTLSServer::kDefaultPort;

auto make_ec2_md() {
  auto socket_factory = std::make_shared<aws::http::IoServiceSocketFactory>();
  auto executor = std::make_shared<aws::utils::IoServiceExecutor>(1);
  return std::make_shared<aws::http::Ec2Metadata>(executor,
                                                  socket_factory,
                                                  "127.0.0.1",
                                                  kPort,
                                                  true);
}

} //namespace

BOOST_AUTO_TEST_SUITE(Ec2Metadata)

BOOST_AUTO_TEST_CASE(GetRegion) {
  aws::kinesis::test::TestTLSServer server;

  server.enqueue_handler([](auto&) {
    aws::http::HttpResponse res(200);
    res.set_data("us-east-1a");
    return res;
  });

  auto metadata = make_ec2_md();

  bool invoked = false;
  metadata->get_region([&](bool success, auto& result) {
    invoked = true;
    BOOST_CHECK(success);
    BOOST_CHECK_EQUAL(result, "us-east-1");
  });
  aws::utils::sleep_for(std::chrono::milliseconds(100));
  BOOST_CHECK_MESSAGE(invoked, "Callback should've been invoked");

  server.enqueue_handler([](auto&) {
    aws::http::HttpResponse res(200);
    res.set_data("sdsfrnesrfht");
    return res;
  });

  invoked = false;
  metadata->get_region([&](bool success, auto& result) {
    invoked = true;
    BOOST_CHECK_MESSAGE(!success, "Should've failed for invalid AZ");
  });

  aws::utils::sleep_for(std::chrono::milliseconds(100));
  BOOST_CHECK_MESSAGE(invoked, "Callback should've been invoked");
}

BOOST_AUTO_TEST_CASE(GetRegionSync) {
  aws::kinesis::test::TestTLSServer server;

  server.enqueue_handler([](auto&) {
    aws::http::HttpResponse res(200);
    res.set_data("us-east-1a");
    return res;
  });

  auto metadata = make_ec2_md();

  auto region = metadata->get_region();
  BOOST_CHECK_MESSAGE(region, "Should've fetched region successfully");
  BOOST_CHECK_EQUAL(*region, "us-east-1");

  aws::utils::sleep_for(std::chrono::milliseconds(200));
}

BOOST_AUTO_TEST_CASE(Timeout) {
  aws::kinesis::test::TestTLSServer server;

  const int n = 3;

  for (int i = 0; i < n; i++) {
    server.enqueue_handler([](auto&) {
      aws::utils::sleep_for(std::chrono::milliseconds(300));
      aws::http::HttpResponse res(200);
      res.set_data("us-east-1a");
      return res;
    });
  }

  auto metadata = make_ec2_md();

  std::atomic<int> invoked(0);
  for (int i = 0; i < n; i++) {
    metadata->get_region([&](bool success, auto& result) {
      invoked++;
      BOOST_CHECK(!success);
    });
  }

  aws::utils::sleep_for(std::chrono::milliseconds(1500));
  BOOST_CHECK_EQUAL(invoked, n);
}

BOOST_AUTO_TEST_CASE(ProfileCredentials) {
  aws::kinesis::test::TestTLSServer server;

  server.enqueue_handler([](auto&) {
    aws::http::HttpResponse res(200);
    res.set_data("myProfile");
    return res;
  });

  server.enqueue_handler([](auto& req) {
    aws::http::HttpResponse res(200);
    BOOST_CHECK_EQUAL(req.path(),
                      "/latest/meta-data/iam/security-credentials/myProfile/");
    res.set_data("abcd");
    return res;
  });

  auto metadata = make_ec2_md();

  bool invoked = false;
  metadata->get_instance_profile_credentials([&](bool success, auto& result) {
    invoked = true;
    BOOST_CHECK(success);
    BOOST_CHECK_EQUAL(result, "abcd");
  });

  aws::utils::sleep_for(std::chrono::milliseconds(100));
  BOOST_CHECK_MESSAGE(invoked, "Callback should've been invoked");
}

BOOST_AUTO_TEST_CASE(ConnectionRejected) {
  auto metadata = make_ec2_md();
  bool invoked = false;
  metadata->get_region([&](bool success, auto& result) {
    invoked = true;
    BOOST_CHECK(!success);
  });
  aws::utils::sleep_for(std::chrono::milliseconds(600));
  BOOST_CHECK_MESSAGE(invoked, "Callback should've been invoked");
}

BOOST_AUTO_TEST_SUITE_END()
