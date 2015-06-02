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
#include <boost/date_time/posix_time/posix_time_duration.hpp>

#include <rapidjson/prettywriter.h>
#include <rapidjson/stringbuffer.h>

#include <aws/kinesis/test/test_tls_server.h>
#include <aws/auth/credentials.h>
#include <aws/utils/utils.h>
#include <aws/utils/io_service_executor.h>
#include <aws/http/io_service_socket.h>
#include <aws/http/ec2_metadata.h>

namespace {

const int kPort = aws::kinesis::test::TestTLSServer::kDefaultPort;

const std::string kDefaultAkid = "AKIAAAAAAAAAAAAAAAAA";
const std::string kDefaultSk = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
const std::string kDefaultToken = "abcd";

std::shared_ptr<aws::auth::InstanceProfileAwsCredentialsProvider>
make_provider() {
  auto executor = std::make_shared<aws::utils::IoServiceExecutor>(1);
  auto socket_factory = std::make_shared<aws::http::IoServiceSocketFactory>();
  auto ec2_metadata =
      std::make_shared<aws::http::Ec2Metadata>(
          executor,
          socket_factory,
          "127.0.0.1",
          kPort,
          true);
  auto provider =
      std::make_shared<aws::auth::InstanceProfileAwsCredentialsProvider>(
          executor,
          ec2_metadata);
  aws::utils::sleep_for(std::chrono::milliseconds(100));
  return provider;
}

void enqueue_credentials_handler(
    aws::kinesis::test::TestTLSServer& server,
    uint64_t expires_from_now_seconds = 3600,
    const std::string& akid = kDefaultAkid,
    const std::string& sk = kDefaultSk,
    const std::string& token = kDefaultToken) {
  server.enqueue_handler([](auto&) {
    aws::http::HttpResponse res(200);
    res.set_data("a");
    return res;
  });

  auto tp = boost::posix_time::microsec_clock::universal_time() +
      boost::posix_time::seconds(expires_from_now_seconds);

  rapidjson::StringBuffer sb;
  rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(sb);

  writer.StartObject();

  writer.String("Code");
  writer.String("Success");

  writer.String("LastUpdated");
  writer.String("2015-04-05T20:41:50Z");

  writer.String("Type");
  writer.String("AWS-HMAC");

  writer.String("AccessKeyId");
  writer.String(akid.c_str());

  writer.String("SecretAccessKey");
  writer.String(sk.c_str());

  writer.String("Token");
  writer.String(token.c_str());

  writer.String("Expiration");
  writer.String(aws::utils::format_ptime(tp).c_str());

  writer.EndObject();

  std::string json = sb.GetString();

  server.enqueue_handler([=](auto&) {
    aws::http::HttpResponse res(200);
    res.set_data(json);
    return res;
  });
}

} //namespace

BOOST_AUTO_TEST_SUITE(Credentials)

BOOST_AUTO_TEST_CASE(InstanceProfileNormal) {
  aws::kinesis::test::TestTLSServer server;
  enqueue_credentials_handler(server);

  auto provider = make_provider();
  auto creds = provider->get_credentials();

  BOOST_CHECK_MESSAGE(creds, "Should have succeeded getting creds");
  BOOST_CHECK_EQUAL(creds->akid, kDefaultAkid);
  BOOST_CHECK_EQUAL(creds->secret_key, kDefaultSk);
  BOOST_CHECK_MESSAGE(creds->session_token,
                      "Should have fetched session token");
  BOOST_CHECK_EQUAL(creds->session_token.get(), kDefaultToken);
}

BOOST_AUTO_TEST_CASE(InstanceProfileFail) {
  auto provider = make_provider();
  auto creds = provider->get_credentials();
  BOOST_CHECK(!creds);
}

BOOST_AUTO_TEST_CASE(InstanceProfileRefresh) {
  aws::kinesis::test::TestTLSServer server;

  // Set an expiration of 3 mins + 2 second. The refresh should happen 2
  // seconds after right now, because InstanceProfileAwsCredentialsProvider
  // refreshes 3 mins before the expiration.
  enqueue_credentials_handler(server, 182);

  // The second time the InstanceProfileAwsCredentialsProvider asks for creds
  // we're going to give it a different set so we can check that a refresh
  // actually happened.
  auto alt_akid = "AKIAAAAAAAAAAAAAABBB";
  auto alt_sk = "baaaaaaaaaaaaaaaaabbaaaaaaaaaaaaaaaaaaab";
  auto alt_token = "1234";
  enqueue_credentials_handler(server, 600, alt_akid, alt_sk, alt_token);

  auto provider = make_provider();
  auto creds = provider->get_credentials();

  BOOST_CHECK_MESSAGE(creds, "Should have succeeded getting creds");
  BOOST_CHECK_EQUAL(creds->akid, kDefaultAkid);
  BOOST_CHECK_EQUAL(creds->secret_key, kDefaultSk);
  BOOST_CHECK_MESSAGE(creds->session_token,
                      "Should have fetched session token");
  BOOST_CHECK_EQUAL(creds->session_token.get(), kDefaultToken);

  // Wait for refresh
  aws::utils::sleep_for(std::chrono::milliseconds(2200));

  creds = provider->get_credentials();

  BOOST_CHECK_MESSAGE(creds, "Should have succeeded getting creds");
  BOOST_CHECK_EQUAL(creds->akid, alt_akid);
  BOOST_CHECK_EQUAL(creds->secret_key, alt_sk);
  BOOST_CHECK_MESSAGE(creds->session_token,
                      "Should have fetched session token");
  BOOST_CHECK_EQUAL(creds->session_token.get(), alt_token);
}

BOOST_AUTO_TEST_CASE(InstanceProfileManualRefresh) {
  aws::kinesis::test::TestTLSServer server;

  enqueue_credentials_handler(server);

  // The second time the InstanceProfileAwsCredentialsProvider asks for creds
  // we're going to give it a different set so we can check that a refresh
  // actually happened.
  auto alt_akid = "AKIAAAAAAAAAAAAAABBB";
  auto alt_sk = "baaaaaaaaaaaaaaaaabbaaaaaaaaaaaaaaaaaaab";
  auto alt_token = "1234";
  enqueue_credentials_handler(server, 6000, alt_akid, alt_sk, alt_token);

  auto provider = make_provider();
  auto creds = provider->get_credentials();

  BOOST_CHECK_MESSAGE(creds, "Should have succeeded getting creds");
  BOOST_CHECK_EQUAL(creds->akid, kDefaultAkid);
  BOOST_CHECK_EQUAL(creds->secret_key, kDefaultSk);
  BOOST_CHECK_MESSAGE(creds->session_token,
                      "Should have fetched session token");
  BOOST_CHECK_EQUAL(creds->session_token.get(), kDefaultToken);

  provider->refresh();
  aws::utils::sleep_for(std::chrono::milliseconds(150));

  creds = provider->get_credentials();

  BOOST_CHECK_MESSAGE(creds, "Should have succeeded getting creds");
  BOOST_CHECK_EQUAL(creds->akid, alt_akid);
  BOOST_CHECK_EQUAL(creds->secret_key, alt_sk);
  BOOST_CHECK_MESSAGE(creds->session_token,
                      "Should have fetched session token");
  BOOST_CHECK_EQUAL(creds->session_token.get(), alt_token);
}

BOOST_AUTO_TEST_SUITE_END()
