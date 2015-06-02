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

#include <map>
#include <regex>

#include <boost/algorithm/string/split.hpp>
#include <boost/test/unit_test.hpp>

#include <glog/logging.h>

#include <aws/metrics/metrics_manager.h>
#include <aws/utils/io_service_executor.h>
#include <aws/utils/utils.h>
#include <aws/http/io_service_socket.h>
#include <aws/kinesis/test/test_tls_server.h>

namespace {

using ExtraDimensions = aws::metrics::detail::ExtraDimensions;

auto make_metrics_manager(ExtraDimensions dims = ExtraDimensions()) {
  auto executor = std::make_shared<aws::utils::IoServiceExecutor>(1);
  auto socket_factory = std::make_shared<aws::http::IoServiceSocketFactory>();
  auto creds =
      std::make_shared<aws::auth::BasicAwsCredentialsProvider>(
          "AKIAAAAAAAAAAAAAAAAA",
          "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
  return
      std::make_shared<aws::metrics::MetricsManager>(
          executor,
          socket_factory,
          creds,
          "us-west-1",
          "TestNamespace",
          aws::metrics::constants::Level::Detailed,
          aws::metrics::constants::Granularity::Shard,
          dims,
          "localhost",
          aws::kinesis::test::TestTLSServer::kDefaultPort,
          std::chrono::milliseconds(50),
          std::chrono::milliseconds(25));
}

std::map<std::string, std::string> parse_query_args(const std::string& s) {
  std::vector<std::string> split_url = aws::utils::split_on_first(s, "?");

  std::map<std::string, std::string> m;

  if (split_url.size() >= 2) {
    std::vector<std::string> v;
    boost::split(v, split_url[1], boost::is_any_of("&"));
    for (const auto& s : v) {
      std::vector<std::string> p;
      boost::split(p, s, boost::is_any_of("="));
      for (auto& a : p) {
        boost::trim(a);
      }
      m.emplace(std::piecewise_construct,
                std::forward_as_tuple(p.at(0)),
                std::forward_as_tuple(p.at(1)));
    }
  }

  return m;
}

} // namespace

BOOST_AUTO_TEST_SUITE(MetricsManager)

BOOST_AUTO_TEST_CASE(RequestGeneration) {
  using aws::metrics::constants::Names;

  aws::kinesis::test::TestTLSServer server;

  std::string path;
  server.enqueue_handler([&](auto& req) {
    path = req.path();

    std::map<std::string, std::string> headers(req.headers().cbegin(),
                                               req.headers().cend());
    BOOST_REQUIRE(headers.find("Authorization") != headers.end());
    BOOST_REQUIRE(headers.find("Host") != headers.end());

    aws::http::HttpResponse res(200);
    return res;
  });

  auto metrics_manager = make_metrics_manager();

  auto m = metrics_manager->finder().set_name(Names::Test)
                                    .set_stream("MyStream")
                                    .find();

  auto m2 = metrics_manager->finder().set_name(Names::Test)
                                     .set_stream("MyStream2")
                                     .set_shard("shard-0")
                                     .find();

  auto m3 = metrics_manager->finder().set_name(Names::Test)
                                     .set_stream("MyStream2")
                                     .set_shard("shard-1")
                                     .find();

  // 0 + 1 + ... + 99 = 4950
  for (int i = 0; i < 100; i++) {
    m->put(i);
    m2->put(i);
    m3->put(i);
  }

  aws::utils::sleep_for(std::chrono::milliseconds(125));
  metrics_manager->stop();
  aws::utils::sleep_for(std::chrono::milliseconds(200));

  BOOST_REQUIRE(!path.empty());

  auto args = parse_query_args(path);

  std::regex date_regex("\\d{4}-\\d{2}-\\d{2}T\\d{2}%3A\\d{2}%3A\\d{2}Z",
                        std::regex::ECMAScript);
  auto check = [&](auto& k, auto& v) {
    try {
      double expected = std::stod(v);
      double actual = std::stod(args.at(k));
      BOOST_CHECK_EQUAL(expected, actual);
    } catch (const std::exception& e) {
      BOOST_CHECK_EQUAL(args.at(k), v);
    }
  };

  try {
    check("Action", "PutMetricData");
    check("Namespace", "TestNamespace");
    check("Version", "2010-08-01");

    // Global stats for metric "Test"
    check("MetricData.member.1.MetricName", "Test");
    check("MetricData.member.1.StatisticValues.Maximum", "99");
    check("MetricData.member.1.StatisticValues.Minimum", "0");
    check("MetricData.member.1.StatisticValues.SampleCount", "300");
    check("MetricData.member.1.StatisticValues.Sum", "14850");
    check("MetricData.member.1.Unit", "Count");
    BOOST_CHECK(std::regex_match(args.at("MetricData.member.2.Timestamp"),
                                 date_regex));

    // Test -> MyStream
    check("MetricData.member.2.Dimensions.member.1.Name", "StreamName");
    check("MetricData.member.2.Dimensions.member.1.Value", "MyStream");
    check("MetricData.member.2.MetricName", "Test");
    check("MetricData.member.2.StatisticValues.Maximum", "99");
    check("MetricData.member.2.StatisticValues.Minimum", "0");
    check("MetricData.member.2.StatisticValues.SampleCount", "100");
    check("MetricData.member.2.StatisticValues.Sum", "4950");
    check("MetricData.member.2.Unit", "Count");
    BOOST_CHECK(std::regex_match(args.at("MetricData.member.2.Timestamp"),
                                 date_regex));

    // Test -> MyStream2
    check("MetricData.member.3.Dimensions.member.1.Name", "StreamName");
    check("MetricData.member.3.Dimensions.member.1.Value", "MyStream2");
    check("MetricData.member.3.MetricName", "Test");
    check("MetricData.member.3.StatisticValues.Maximum", "99");
    check("MetricData.member.3.StatisticValues.Minimum", "0");
    check("MetricData.member.3.StatisticValues.SampleCount", "200");
    check("MetricData.member.3.StatisticValues.Sum", "9900");
    check("MetricData.member.3.Unit", "Count");
    BOOST_CHECK(std::regex_match(args.at("MetricData.member.3.Timestamp"),
                                 date_regex));

    // Test -> MyStream2 -> shard-0
    check("MetricData.member.4.Dimensions.member.1.Name", "StreamName");
    check("MetricData.member.4.Dimensions.member.1.Value", "MyStream2");
    check("MetricData.member.4.Dimensions.member.2.Name", "ShardId");
    check("MetricData.member.4.Dimensions.member.2.Value", "shard-0");
    check("MetricData.member.4.MetricName", "Test");
    check("MetricData.member.4.StatisticValues.Maximum", "99");
    check("MetricData.member.4.StatisticValues.Minimum", "0");
    check("MetricData.member.4.StatisticValues.SampleCount", "100");
    check("MetricData.member.4.StatisticValues.Sum", "4950");
    check("MetricData.member.4.Unit", "Count");
    BOOST_CHECK(std::regex_match(args.at("MetricData.member.4.Timestamp"),
                                 date_regex));

    // Test -> MyStream2 -> shard-1
    check("MetricData.member.5.Dimensions.member.1.Name", "StreamName");
    check("MetricData.member.5.Dimensions.member.1.Value", "MyStream2");
    check("MetricData.member.5.Dimensions.member.2.Name", "ShardId");
    check("MetricData.member.5.Dimensions.member.2.Value", "shard-1");
    check("MetricData.member.5.MetricName", "Test");
    check("MetricData.member.5.StatisticValues.Maximum", "99");
    check("MetricData.member.5.StatisticValues.Minimum", "0");
    check("MetricData.member.5.StatisticValues.SampleCount", "100");
    check("MetricData.member.5.StatisticValues.Sum", "4950");
    check("MetricData.member.5.Unit", "Count");
    BOOST_CHECK(std::regex_match(args.at("MetricData.member.5.Timestamp"),
                                 date_regex));
  } catch (const std::exception& e) {
    BOOST_FAIL(e.what());
  }
}

BOOST_AUTO_TEST_CASE(ExtraDimensions) {
  auto get_args = [](aws::metrics::detail::ExtraDimensions extra_dims,
                     std::string stream = "",
                     std::string shard = "") {
    aws::kinesis::test::TestTLSServer server;

    std::string path;
    server.enqueue_handler([&](auto& req) {
      path = req.path();
      aws::http::HttpResponse res(200);
      res.set_data("ok");
      return res;
    });

    auto metrics_manager = make_metrics_manager(extra_dims);

    auto mf = metrics_manager->finder().set_name("Test");
    if (!stream.empty()) {
      mf.set_stream(stream);
    }
    if (!shard.empty()) {
      mf.set_shard(shard);
    }
    mf.find()->put(0);

    aws::utils::sleep_for(std::chrono::milliseconds(100));
    metrics_manager->stop();
    aws::utils::sleep_for(std::chrono::milliseconds(500));

    BOOST_REQUIRE(!path.empty());

    return parse_query_args(path);
  };

  auto check = [](auto& args, int m, int d, auto& k, auto& v) {
    std::stringstream ss;
    ss << "MetricData.member." << m << ".Dimensions.member." << d << ".";
    auto prefix = ss.str();
    BOOST_CHECK_EQUAL(args.at(prefix + "Name"), k);
    BOOST_CHECK_EQUAL(args.at(prefix + "Value"), v);
  };

  auto check_no_dim = [](auto& args, int m, int d) {
    std::stringstream ss;
    ss << "MetricData.member." << m << ".Dimensions.member." << d << ".";
    auto prefix = ss.str();
    BOOST_CHECK(args.find(prefix + "Name") == args.end());
    BOOST_CHECK(args.find(prefix + "Value") == args.end());
  };

  auto check_no_member = [](auto& args, int m) {
    std::stringstream ss;
    ss << "MetricData.member." << m << ".MetricName";
    BOOST_CHECK(args.find(ss.str()) == args.end());
  };

  using ExtraDimTuple = std::tuple<std::string,
                                   std::string,
                                   aws::metrics::constants::Granularity>;

  {
    auto args =
        get_args(
            boost::assign::list_of<ExtraDimTuple>
                ("a", "1", aws::metrics::constants::Granularity::Global)
                ("b", "2", aws::metrics::constants::Granularity::Stream)
                ("c", "3", aws::metrics::constants::Granularity::Shard),
            "MyStream",
            "0");

    check_no_dim(args, 1, 1);

    check(args, 2, 1, "a", "1");
    check_no_dim(args, 2, 2);

    check(args, 3, 1, "a", "1");
    check(args, 3, 2, "StreamName", "MyStream");
    check_no_dim(args, 3, 3);

    check(args, 4, 1, "a", "1");
    check(args, 4, 2, "StreamName", "MyStream");
    check(args, 4, 3, "b", "2");
    check_no_dim(args, 3, 4);

    check(args, 5, 1, "a", "1");
    check(args, 5, 2, "StreamName", "MyStream");
    check(args, 5, 3, "b", "2");
    check(args, 5, 4, "ShardId", "0");
    check_no_dim(args, 5, 5);

    check(args, 6, 1, "a", "1");
    check(args, 6, 2, "StreamName", "MyStream");
    check(args, 6, 3, "b", "2");
    check(args, 6, 4, "ShardId", "0");
    check(args, 6, 5, "c", "3");
    check_no_dim(args, 6, 6);

    check_no_member(args, 7);
  }

  {
    auto args =
        get_args(
            boost::assign::list_of<ExtraDimTuple>
                ("a", "1", aws::metrics::constants::Granularity::Global)
                ("b", "2", aws::metrics::constants::Granularity::Stream)
                ("c", "3", aws::metrics::constants::Granularity::Shard),
            "MyStream");

    check_no_dim(args, 1, 1);

    check(args, 2, 1, "a", "1");
    check_no_dim(args, 2, 2);

    check(args, 3, 1, "a", "1");
    check(args, 3, 2, "StreamName", "MyStream");
    check_no_dim(args, 3, 3);

    check(args, 4, 1, "a", "1");
    check(args, 4, 2, "StreamName", "MyStream");
    check(args, 4, 3, "b", "2");
    check_no_dim(args, 3, 4);

    check_no_member(args, 5);
  }

  {
    auto args =
        get_args(
            boost::assign::list_of<ExtraDimTuple>
                ("a", "1", aws::metrics::constants::Granularity::Global)
                ("b", "2", aws::metrics::constants::Granularity::Stream)
                ("c", "3", aws::metrics::constants::Granularity::Shard));

    check_no_dim(args, 1, 1);

    check(args, 2, 1, "a", "1");
    check_no_dim(args, 2, 2);

    check_no_member(args, 3);
  }

  {
    auto args =
        get_args(
            boost::assign::list_of<ExtraDimTuple>
                ("a", "1", aws::metrics::constants::Granularity::Global)
                ("b", "2", aws::metrics::constants::Granularity::Global)
                ("c", "3", aws::metrics::constants::Granularity::Stream)
                ("d", "4", aws::metrics::constants::Granularity::Stream)
                ("e", "5", aws::metrics::constants::Granularity::Shard)
                ("f", "6", aws::metrics::constants::Granularity::Shard),
            "MyStream",
            "0");

    check_no_dim(args, 1, 1);

    check(args, 2, 1, "a", "1");
    check_no_dim(args, 2, 2);

    check(args, 3, 1, "a", "1");
    check(args, 3, 2, "b", "2");
    check_no_dim(args, 3, 3);

    check(args, 4, 1, "a", "1");
    check(args, 4, 2, "b", "2");
    check(args, 4, 3, "StreamName", "MyStream");
    check_no_dim(args, 4, 4);

    check(args, 5, 1, "a", "1");
    check(args, 5, 2, "b", "2");
    check(args, 5, 3, "StreamName", "MyStream");
    check(args, 5, 4, "c", "3");
    check_no_dim(args, 5, 5);

    check(args, 6, 1, "a", "1");
    check(args, 6, 2, "b", "2");
    check(args, 6, 3, "StreamName", "MyStream");
    check(args, 6, 4, "c", "3");
    check(args, 6, 5, "d", "4");
    check_no_dim(args, 6, 6);

    check(args, 7, 1, "a", "1");
    check(args, 7, 2, "b", "2");
    check(args, 7, 3, "StreamName", "MyStream");
    check(args, 7, 4, "c", "3");
    check(args, 7, 5, "d", "4");
    check(args, 7, 6, "ShardId", "0");
    check_no_dim(args, 7, 7);

    check(args, 8, 1, "a", "1");
    check(args, 8, 2, "b", "2");
    check(args, 8, 3, "StreamName", "MyStream");
    check(args, 8, 4, "c", "3");
    check(args, 8, 5, "d", "4");
    check(args, 8, 6, "ShardId", "0");
    check(args, 8, 7, "e", "5");
    check_no_dim(args, 8, 8);

    check(args, 9, 1, "a", "1");
    check(args, 9, 2, "b", "2");
    check(args, 9, 3, "StreamName", "MyStream");
    check(args, 9, 4, "c", "3");
    check(args, 9, 5, "d", "4");
    check(args, 9, 6, "ShardId", "0");
    check(args, 9, 7, "e", "5");
    check(args, 9, 8, "f", "6");
    check_no_dim(args, 9, 9);

    check_no_member(args, 10);
  }
}

BOOST_AUTO_TEST_CASE(UploadFrequency) {
  aws::kinesis::test::TestTLSServer server;

  std::atomic<int> count(0);
  for (int i = 0; i < 50; i++) {
    server.enqueue_handler([&](auto& req) {
      count++;
      aws::http::HttpResponse res(200);
      res.set_data("ok");
      return res;
    });
  }

  auto metrics_manager = make_metrics_manager();

  metrics_manager
      ->finder()
      .set_name(aws::metrics::constants::Names::Test)
      .set_stream("MyStream")
      .find()
      ->put(0);

  aws::utils::sleep_for(std::chrono::milliseconds(225));
  metrics_manager->stop();
  aws::utils::sleep_for(std::chrono::milliseconds(500));

  BOOST_CHECK_EQUAL(count, 4);
}

BOOST_AUTO_TEST_CASE(Retry) {
  using aws::metrics::constants::Names;

  aws::kinesis::test::TestTLSServer server;

  server.enqueue_handler([](auto& req) {
    aws::http::HttpResponse res(500);
    res.set_data("test fail");
    return res;
  });

  bool retried = false;
  server.enqueue_handler([&](auto& req) {
    retried = true;
    aws::http::HttpResponse res(200);
    res.set_data("ok");
    return res;
  });

  auto metrics_manager = make_metrics_manager();

  metrics_manager
      ->finder()
      .set_name(aws::metrics::constants::Names::Test)
      .set_stream("MyStream")
      .find()
      ->put(0);

  aws::utils::sleep_for(std::chrono::milliseconds(80));
  metrics_manager->stop();
  aws::utils::sleep_for(std::chrono::milliseconds(500));

  BOOST_CHECK(retried);
}

BOOST_AUTO_TEST_SUITE_END()
