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

#include <aws/kinesis/test/test_tls_server.h>
#include <aws/http/http_client.h>
#include <aws/http/http_request.h>

#include <aws/http/http_client.h>
#include <aws/http/io_service_socket.h>
#include <aws/utils/io_service_executor.h>
#include <aws/utils/utils.h>

namespace {

const int kPort = aws::kinesis::test::TestTLSServer::kDefaultPort;

auto make_socket_factory() {
  return std::make_shared<aws::http::IoServiceSocketFactory>();
}

auto make_executor() {
  return std::make_shared<aws::utils::IoServiceExecutor>(1);
}

} //namepspace

BOOST_AUTO_TEST_SUITE(HttpClient)

// Test that the min number of connections get opened on init
BOOST_AUTO_TEST_CASE(MinConnection) {
  aws::kinesis::test::TestTLSServer server;
  for (size_t n = 1; n < 4; n++) {
    auto e = make_executor();
    auto f = make_socket_factory();
    aws::http::HttpClient client(e, f, "localhost", kPort, true, false, n, n);
    aws::utils::sleep_for(std::chrono::milliseconds(100));
    BOOST_CHECK_EQUAL(n, client.available_connections());
    BOOST_CHECK_EQUAL(n, client.total_connections());
  }
}

// Test that the number of connections grow when there are lots of requests,
// but not beyond the limit
BOOST_AUTO_TEST_CASE(NewConnections) {
  aws::kinesis::test::TestTLSServer server;

  auto e = make_executor();
  auto f = make_socket_factory();
  aws::http::HttpClient client(e, f, "localhost", kPort, true, false, 1, 8);

  aws::http::HttpRequest req("GET", "/");
  for (int i = 0; i < 16; i++) {
    client.put(req);
  }

  aws::utils::sleep_for(std::chrono::milliseconds(5));
  BOOST_CHECK_EQUAL(8, client.total_connections());
  aws::utils::sleep_for(std::chrono::milliseconds(100));
}

// Test that a connection becomes available again after finishing a request
BOOST_AUTO_TEST_CASE(ConnectionReuse) {
  aws::kinesis::test::TestTLSServer server;

  auto e = make_executor();
  auto f = make_socket_factory();
  aws::http::HttpClient client(e, f, "localhost", kPort, true, false, 1, 1);

  aws::http::HttpRequest req("GET", "/");
  client.put(req);

  BOOST_CHECK_EQUAL(0, client.available_connections());
  aws::utils::sleep_for(std::chrono::milliseconds(100));
  BOOST_CHECK_EQUAL(1, client.available_connections());
}

// Make request to server and get an echo
BOOST_AUTO_TEST_CASE(Basic) {
  aws::kinesis::test::TestTLSServer server;

  auto e = make_executor();
  auto f = make_socket_factory();
  aws::http::HttpClient client(e, f, "localhost", kPort, true, false);

  aws::http::HttpRequest req("GET", "/");
  req.add_header("a", "b");
  req.add_header("c", "d");
  const char* data = "hello world";
  req.set_data(data);
  bool read = false;
  client.put(req, [&](const auto& result) {
    read = true;
    LOG(INFO) << result->error();
    BOOST_REQUIRE(result->successful());

    BOOST_CHECK_EQUAL(result->status_code(), 200);
    BOOST_CHECK_EQUAL(result->response_body(), data);

    auto& headers = result->headers();
    BOOST_CHECK_EQUAL(headers.size(), 3);
    BOOST_CHECK_EQUAL(headers[0].first, "a");
    BOOST_CHECK_EQUAL(headers[0].second, "b");
    BOOST_CHECK_EQUAL(headers[1].first, "c");
    BOOST_CHECK_EQUAL(headers[1].second, "d");
    BOOST_CHECK_EQUAL(headers[2].first, "Content-Length");
    BOOST_CHECK_EQUAL(headers[2].second, std::to_string(std::strlen(data)));
  });

  aws::utils::sleep_for(std::chrono::milliseconds(150));
  BOOST_CHECK(read);
}

// Check that the context pointer gets passed back with the result
BOOST_AUTO_TEST_CASE(Context) {
  aws::kinesis::test::TestTLSServer server;

  auto e = make_executor();
  auto f = make_socket_factory();
  aws::http::HttpClient client(e, f, "localhost", kPort, true, false);

  auto ctx = std::make_shared<std::string>("hello");
  auto ctx_copy = ctx;

  bool read = false;
  aws::http::HttpRequest req("GET", "/");
  client.put(
      req,
      [&](const auto& result) {;
        read = true;
        BOOST_CHECK_EQUAL(result->template context<std::string>(), ctx_copy);
      },
      std::move(ctx));
  BOOST_CHECK_MESSAGE(!ctx, "Shared pointer should have been moved");

  aws::utils::sleep_for(std::chrono::milliseconds(150));
  BOOST_CHECK(read);
}

// Check that requests with closer deadlines are executed first
BOOST_AUTO_TEST_CASE(Priority) {
  aws::kinesis::test::TestTLSServer server;

  auto e = make_executor();
  auto f = make_socket_factory();
  aws::http::HttpClient client(e, f, "localhost", kPort, true, false, 1, 1);

  std::atomic<size_t> counter(0);

  aws::http::HttpRequest req("GET", "/");
  // These requests have a deadline 100ms from now
  for (int i = 0; i < 8; i++) {
    client.put(
        req,
        [&](auto) { counter++; },
        std::shared_ptr<void>(),
        std::chrono::steady_clock::now() + std::chrono::milliseconds(100));
  }

  // This request has a deadline that is now. It should go to the front of the
  // queue and get sent before most of the others.
  client.put(
      req,
      [&](auto) {
        BOOST_CHECK_MESSAGE(
            counter < 8,
            "The request with a closer deadline should have finished before "
            "those with later ones.");
      },
      std::shared_ptr<void>(),
      std::chrono::steady_clock::now());

  aws::utils::sleep_for(std::chrono::milliseconds(250));
  BOOST_CHECK_EQUAL(counter, 8);
}

// Test that request timeout works
BOOST_AUTO_TEST_CASE(RequestTimeout) {
  aws::kinesis::test::TestTLSServer server;

  // Make the server delay response by 100ms
  server.enqueue_handler([](const auto& request) {
    aws::utils::sleep_for(std::chrono::milliseconds(100));
    aws::http::HttpResponse res(200);
    res.set_data("");
    return res;
  });

  // Configure client with 50 ms request timeout
  auto e = make_executor();
  auto f = make_socket_factory();
  auto fiddy = std::chrono::milliseconds(50);
  aws::http::HttpClient client(
      e, f, "localhost", kPort, true, false, 1, 1, fiddy, fiddy);

  auto ctx = std::make_shared<std::string>("hello");
  auto ctx_copy = ctx;

  aws::http::HttpRequest req("GET", "/");
  bool timed_out = false;
  client.put(
      req,
      [&](const auto& result) {
        timed_out = true;
        BOOST_CHECK_EQUAL(result->successful(), false);
      });

  aws::utils::sleep_for(std::chrono::milliseconds(150));

  BOOST_CHECK(timed_out);
}

// Measure the TPS to make sure it's not completely broken
BOOST_AUTO_TEST_CASE(Throughput) {
  aws::kinesis::test::TestTLSServer server;

  auto e = make_executor();
  auto f = make_socket_factory();
  aws::http::HttpClient client(e, f, "localhost", kPort, true, false, 1, 4);

  aws::http::HttpRequest req("GET", "/");
  req.set_data(std::string(8 * 1024, 'a'));

  std::atomic<size_t> counter(0);
  std::chrono::steady_clock::time_point start =
      std::chrono::steady_clock::now();
  std::chrono::steady_clock::time_point end;

  LOG(INFO) << "Starting http client throughput test...";

  size_t N = 20000;
  for (size_t i = 0; i < N; i++) {
    client.put(
        req,
        [&](auto& result) {
          if (!result->successful()) {
            BOOST_FAIL(result->error());
          }
          if (++counter == N) {
            end = std::chrono::steady_clock::now();
          }
        });
  }

  while (counter < N) {
    aws::utils::sleep_for(std::chrono::seconds(1));
    LOG(INFO) << counter << " / " << N;
  }

  double seconds =
      std::chrono::duration_cast<std::chrono::nanoseconds>(end - start)
          .count() / 1e9;
  double rate = (double) N / seconds;
  LOG(INFO) << "Request/response rate: " << rate << " rps";

  BOOST_CHECK_MESSAGE(
      rate >= 2000,
      "TPS is below 2K, something's wrong. It should at least 3K in debug, and "
      "8K in release (on a recent macbook).");
}

BOOST_AUTO_TEST_SUITE_END()
