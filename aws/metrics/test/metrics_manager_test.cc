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

#include <map>
#include <regex>

#include <boost/test/unit_test.hpp>

#include <aws/core/NoResult.h>
#include <aws/metrics/metrics_manager.h>
#include <aws/monitoring/model/PutMetricDataRequest.h>
#include <aws/utils/io_service_executor.h>
#include <aws/utils/logging.h>
#include <aws/utils/utils.h>

namespace {

const constexpr size_t kUploadFreqMs = 1000;

using ExtraDimensions = aws::metrics::detail::ExtraDimensions;

Aws::Client::ClientConfiguration fake_client_cfg() {
  Aws::Client::ClientConfiguration cfg;
  cfg.region = "us-west-1";
  cfg.endpointOverride = "localhost:61666";
  return cfg;
}

template <typename U>
void push(const std::list<U>* t, const U& v) {
  ((std::list<U>*) t)->push_back(v);
}

class MockCloudWatchClient : public Aws::CloudWatch::CloudWatchClient {
 public:
  MockCloudWatchClient(int num_failures = 0)
      : Aws::CloudWatch::CloudWatchClient(fake_client_cfg()),
        num_failures_(num_failures),
        executor_(std::make_shared<aws::utils::IoServiceExecutor>(1)) {}

  virtual void PutMetricDataAsync(
      const Aws::CloudWatch::Model::PutMetricDataRequest& request,
      const Aws::CloudWatch::PutMetricDataResponseReceivedHandler& handler,
      const std::shared_ptr<const Aws::Client::AsyncCallerContext>& context
          = nullptr) const override {
    push(&requests_, request);
    executor_->submit([=] {
      if (num_failures_ <= 0) {
        Aws::NoResult nr;
        Aws::CloudWatch::Model::PutMetricDataOutcome o(nr);
        handler(this, request, o, context);
      } else {
        (*((int*) &num_failures_))--;
        Aws::Client::AWSError<Aws::CloudWatch::CloudWatchErrors> e;
        Aws::CloudWatch::Model::PutMetricDataOutcome o(e);
        handler(this, request, o, context);
      }
    });
  }

  std::list<Aws::CloudWatch::Model::PutMetricDataRequest> requests() const {
    return requests_;
  }

 private:
  int num_failures_;
  std::list<Aws::CloudWatch::Model::PutMetricDataRequest> requests_;
  std::shared_ptr<aws::utils::Executor> executor_;
};

auto make_metrics_manager(std::shared_ptr<MockCloudWatchClient> mock_cw,
                          ExtraDimensions dims = ExtraDimensions()) {
  return std::make_shared<aws::metrics::MetricsManager>(
      std::make_shared<aws::utils::IoServiceExecutor>(1),
      mock_cw,
      "TestNamespace",
      aws::metrics::constants::Level::Detailed,
      aws::metrics::constants::Granularity::Shard,
      dims,
      std::chrono::milliseconds(kUploadFreqMs),
      std::chrono::milliseconds(100));
}

void check_dims(const Aws::CloudWatch::Model::MetricDatum& metric_datum,
                std::initializer_list<std::string> expectation) {
  BOOST_REQUIRE_EQUAL(expectation.size() % 2, 0);

  auto dims = metric_datum.GetDimensions();
  BOOST_REQUIRE_EQUAL(expectation.size() / 2, dims.size());

  int i = 0;
  bool is_name = true;
  for (auto& s : expectation) {
    if (is_name) {
      is_name = false;
      BOOST_CHECK_EQUAL(dims[i].GetName(), s);
    } else {
      is_name = true;
      BOOST_CHECK_EQUAL(dims[i].GetValue(), s);
      i++;
    }
  }
};

} // namespace

BOOST_AUTO_TEST_SUITE(MetricsManager)

BOOST_AUTO_TEST_CASE(RequestGeneration) {
  using aws::metrics::constants::Names;

  auto mock_cw = std::make_shared<MockCloudWatchClient>();
  auto metrics_manager = make_metrics_manager(mock_cw);

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

  aws::utils::sleep_for(std::chrono::milliseconds(1500));
  metrics_manager->stop();
  aws::utils::sleep_for(std::chrono::milliseconds(200));

  BOOST_REQUIRE(mock_cw->requests().size() == 1);

  auto req = mock_cw->requests().front();
  BOOST_CHECK_EQUAL("TestNamespace", req.GetNamespace());

  auto data = req.GetMetricData();
  BOOST_REQUIRE_EQUAL(data.size(), 5);

  std::regex date_regex("\\d{4}-\\d{2}-\\d{2}T\\d{2}:\\d{2}:\\d{2}Z",
                        std::regex::ECMAScript);

  // Global stats for metric "Test"
  {
    auto d = data[0];
    BOOST_CHECK_EQUAL(d.GetMetricName(), "Test");
    BOOST_CHECK(d.GetUnit() == Aws::CloudWatch::Model::StandardUnit::Count);

    check_dims(d, {});

    auto stats = d.GetStatisticValues();
    BOOST_CHECK_EQUAL(stats.GetSum(), 14850);
    BOOST_CHECK_EQUAL(stats.GetMinimum(), 0);
    BOOST_CHECK_EQUAL(stats.GetMaximum(), 99);
    BOOST_CHECK_EQUAL(stats.GetSampleCount(), 300);
  }

  // Test -> MyStream
  {
    auto d = data[1];
    BOOST_CHECK_EQUAL(d.GetMetricName(), "Test");
    BOOST_CHECK(d.GetUnit() == Aws::CloudWatch::Model::StandardUnit::Count);

    check_dims(d, { "StreamName", "MyStream" });

    auto stats = d.GetStatisticValues();
    BOOST_CHECK_EQUAL(stats.GetSum(), 4950);
    BOOST_CHECK_EQUAL(stats.GetMinimum(), 0);
    BOOST_CHECK_EQUAL(stats.GetMaximum(), 99);
    BOOST_CHECK_EQUAL(stats.GetSampleCount(), 100);
  }

  // Test -> MyStream2
  {
    auto d = data[2];
    BOOST_CHECK_EQUAL(d.GetMetricName(), "Test");
    BOOST_CHECK(d.GetUnit() == Aws::CloudWatch::Model::StandardUnit::Count);

    check_dims(d, { "StreamName", "MyStream2" });

    auto stats = d.GetStatisticValues();
    BOOST_CHECK_EQUAL(stats.GetSum(), 9900);
    BOOST_CHECK_EQUAL(stats.GetMinimum(), 0);
    BOOST_CHECK_EQUAL(stats.GetMaximum(), 99);
    BOOST_CHECK_EQUAL(stats.GetSampleCount(), 200);
  }

  // Test -> MyStream2 -> shard-0
  {
    auto d = data[3];
    BOOST_CHECK_EQUAL(d.GetMetricName(), "Test");
    BOOST_CHECK(d.GetUnit() == Aws::CloudWatch::Model::StandardUnit::Count);

    check_dims(d, {
      "StreamName", "MyStream2",
      "ShardId", "shard-0",
    });

    auto stats = d.GetStatisticValues();
    BOOST_CHECK_EQUAL(stats.GetSum(), 4950);
    BOOST_CHECK_EQUAL(stats.GetMinimum(), 0);
    BOOST_CHECK_EQUAL(stats.GetMaximum(), 99);
    BOOST_CHECK_EQUAL(stats.GetSampleCount(), 100);
  }

  // Test -> MyStream2 -> shard-1
  {
    auto d = data[4];
    BOOST_CHECK_EQUAL(d.GetMetricName(), "Test");
    BOOST_CHECK(d.GetUnit() == Aws::CloudWatch::Model::StandardUnit::Count);

    check_dims(d, {
      "StreamName", "MyStream2",
      "ShardId", "shard-1",
    });

    auto stats = d.GetStatisticValues();
    BOOST_CHECK_EQUAL(stats.GetSum(), 4950);
    BOOST_CHECK_EQUAL(stats.GetMinimum(), 0);
    BOOST_CHECK_EQUAL(stats.GetMaximum(), 99);
    BOOST_CHECK_EQUAL(stats.GetSampleCount(), 100);
  }
}

BOOST_AUTO_TEST_CASE(ExtraDimensions) {
  auto get_req = [](aws::metrics::detail::ExtraDimensions extra_dims,
                    std::string stream = "",
                    std::string shard = "") {
    auto mock_cw = std::make_shared<MockCloudWatchClient>();
    auto metrics_manager = make_metrics_manager(mock_cw, extra_dims);

    auto mf = metrics_manager->finder().set_name("Test");
    if (!stream.empty()) {
      mf.set_stream(stream);
    }
    if (!shard.empty()) {
      mf.set_shard(shard);
    }
    mf.find()->put(0);

    aws::utils::sleep_for(std::chrono::milliseconds(1500));
    metrics_manager->stop();
    aws::utils::sleep_for(std::chrono::milliseconds(500));

    BOOST_REQUIRE_EQUAL(mock_cw->requests().size(), 1);
    return mock_cw->requests().front();
  };

  using ExtraDimTuple = std::tuple<std::string,
                                   std::string,
                                   aws::metrics::constants::Granularity>;

  {
    auto req = get_req(
        boost::assign::list_of<ExtraDimTuple>
            ("a", "1", aws::metrics::constants::Granularity::Global)
            ("b", "2", aws::metrics::constants::Granularity::Stream)
            ("c", "3", aws::metrics::constants::Granularity::Shard),
        "MyStream",
        "shard-0");

    auto data = req.GetMetricData();
    BOOST_REQUIRE_EQUAL(data.size(), 6);

    check_dims(data[0], {});

    check_dims(data[1], {
      "a", "1"
    });

    check_dims(data[2], {
      "a", "1",
      "StreamName", "MyStream"
    });

    check_dims(data[3], {
      "a", "1",
      "StreamName", "MyStream",
      "b", "2"
    });

    check_dims(data[4], {
      "a", "1",
      "StreamName", "MyStream",
      "b", "2",
      "ShardId", "shard-0"
    });

    check_dims(data[5], {
      "a", "1",
      "StreamName", "MyStream",
      "b", "2",
      "ShardId", "shard-0",
      "c", "3"
    });
  }

  {
    auto req = get_req(
        boost::assign::list_of<ExtraDimTuple>
            ("a", "1", aws::metrics::constants::Granularity::Global)
            ("b", "2", aws::metrics::constants::Granularity::Stream)
            ("c", "3", aws::metrics::constants::Granularity::Shard),
        "MyStream");

    auto data = req.GetMetricData();
    BOOST_REQUIRE_EQUAL(data.size(), 4);

    check_dims(data[0], {});

    check_dims(data[1], {
      "a", "1"
    });

    check_dims(data[2], {
      "a", "1",
      "StreamName", "MyStream"
    });

    check_dims(data[3], {
      "a", "1",
      "StreamName", "MyStream",
      "b", "2",
    });
  }

  {
    auto req = get_req(
        boost::assign::list_of<ExtraDimTuple>
            ("a", "1", aws::metrics::constants::Granularity::Global)
            ("b", "2", aws::metrics::constants::Granularity::Stream)
            ("c", "3", aws::metrics::constants::Granularity::Shard));

    auto data = req.GetMetricData();
    BOOST_REQUIRE_EQUAL(data.size(), 2);

    check_dims(data[0], {});

    check_dims(data[1], {
      "a", "1"
    });
  }

  {
    auto req = get_req(
        boost::assign::list_of<ExtraDimTuple>
            ("a", "1", aws::metrics::constants::Granularity::Global)
            ("b", "2", aws::metrics::constants::Granularity::Global)
            ("c", "3", aws::metrics::constants::Granularity::Stream)
            ("d", "4", aws::metrics::constants::Granularity::Stream)
            ("e", "5", aws::metrics::constants::Granularity::Shard)
            ("f", "6", aws::metrics::constants::Granularity::Shard),
        "MyStream",
        "shard-0");

    auto data = req.GetMetricData();
    BOOST_REQUIRE_EQUAL(data.size(), 9);

    check_dims(data[0], {});

    check_dims(data[1], {
      "a", "1"
    });

    check_dims(data[2], {
      "a", "1",
      "b", "2"
    });

    check_dims(data[3], {
      "a", "1",
      "b", "2",
      "StreamName", "MyStream"
    });

    check_dims(data[4], {
      "a", "1",
      "b", "2",
      "StreamName", "MyStream",
      "c", "3"
    });

    check_dims(data[5], {
      "a", "1",
      "b", "2",
      "StreamName", "MyStream",
      "c", "3",
      "d", "4"
    });

    check_dims(data[6], {
      "a", "1",
      "b", "2",
      "StreamName", "MyStream",
      "c", "3",
      "d", "4",
      "ShardId", "shard-0"
    });

    check_dims(data[7], {
      "a", "1",
      "b", "2",
      "StreamName", "MyStream",
      "c", "3",
      "d", "4",
      "ShardId", "shard-0",
      "e", "5"
    });

    check_dims(data[8], {
      "a", "1",
      "b", "2",
      "StreamName", "MyStream",
      "c", "3",
      "d", "4",
      "ShardId", "shard-0",
      "e", "5",
      "f", "6"
    });
  }
}

BOOST_AUTO_TEST_CASE(UploadFrequency) {
  auto mock_cw = std::make_shared<MockCloudWatchClient>();
  auto metrics_manager = make_metrics_manager(mock_cw);

  metrics_manager
      ->finder()
      .set_name(aws::metrics::constants::Names::Test)
      .set_stream("MyStream")
      .find()
      ->put(0);

  std::chrono::high_resolution_clock::time_point start;

  // Wait for 4 requests to be made
  while (mock_cw->requests().size() < 4) {
    if (mock_cw->requests().size() == 1 &&
        start.time_since_epoch().count() == 0) {
      start = std::chrono::high_resolution_clock::now();
    }
    aws::this_thread::yield();
  }
  metrics_manager->stop();

  BOOST_CHECK_CLOSE(aws::utils::seconds_since(start),
                    (double) kUploadFreqMs * 3 / 1000,
                    10);

  aws::utils::sleep_for(std::chrono::milliseconds(200));
}

BOOST_AUTO_TEST_CASE(Retry) {
  using aws::metrics::constants::Names;

  auto mock_cw = std::make_shared<MockCloudWatchClient>(1);
  auto metrics_manager = make_metrics_manager(mock_cw);

  metrics_manager
      ->finder()
      .set_name(aws::metrics::constants::Names::Test)
      .set_stream("MyStream")
      .find()
      ->put(0);

  while (mock_cw->requests().size() < 1) {
    aws::this_thread::yield();
  }

  aws::utils::sleep_for(std::chrono::milliseconds(200));
  metrics_manager->stop();
  aws::utils::sleep_for(std::chrono::milliseconds(500));

  BOOST_CHECK_EQUAL(mock_cw->requests().size(), 2);
}

BOOST_AUTO_TEST_SUITE_END()
