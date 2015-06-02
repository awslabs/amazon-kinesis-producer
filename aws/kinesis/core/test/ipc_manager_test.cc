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

#include <aws/kinesis/core/test/test_utils.h>
#include <aws/kinesis/core/ipc_manager.h>
#include <aws/utils/utils.h>

BOOST_AUTO_TEST_SUITE(IpcManager)

// We hook two IpcManagers up, sending data from one to the other. We check that
// the data read is the same as the data wrote. We test different data sizes,
// including those near and beyond the allowed range.
BOOST_AUTO_TEST_CASE(DataIntegrity) {
  aws::kinesis::test::Fifo pipe;

  auto reader =
    std::make_shared<aws::kinesis::core::IpcManager>(
        std::make_shared<aws::kinesis::core::detail::IpcChannel>(
            pipe,
            nullptr));

  auto writer =
    std::make_shared<aws::kinesis::core::IpcManager>(
        std::make_shared<aws::kinesis::core::detail::IpcChannel>(
            nullptr,
            pipe));

  std::vector<std::string> read;
  std::vector<std::string> wrote;

  auto f = [&](size_t sz) {
    std::string data = aws::kinesis::test::random_string(sz);
    std::string data_copy = data;
    writer->put(std::move(data));
    wrote.push_back(std::move(data_copy));
  };

  for (size_t i = 0; i < 1024; i++) {
    f(i);
  }

  for (size_t i = 2 * 1024 * 1024 - 8; i <= 2 * 1024 * 1024; i++) {
    f(i);
  }

  for (size_t i = 2 * 1024 * 1024 + 1; i < 2 * 1024 * 1024 + 8; i++) {
    try {
      f(i);
      BOOST_FAIL("Shoudld've failed for data larger than 2MB");
    } catch (const std::exception& e) {
      // ok
    }
  }

  aws::utils::sleep_for(std::chrono::milliseconds(500));

  std::string s;
  while (reader->try_take(s)) {
    read.push_back(s);
  }

  BOOST_CHECK_EQUAL(read.size(), wrote.size());
  for (size_t i = 0; i < read.size(); i++) {
    BOOST_CHECK_EQUAL(read[i], wrote[i]);
  }

  writer->shutdown();
  reader->shutdown();
  writer->close_write_channel();
  aws::utils::sleep_for(std::chrono::milliseconds(50));
}

// Measure how many pairs of messages can be read/written per second.
BOOST_AUTO_TEST_CASE(Throughput) {
  aws::kinesis::test::Fifo pipe;

  auto reader =
    std::make_shared<aws::kinesis::core::IpcManager>(
        std::make_shared<aws::kinesis::core::detail::IpcChannel>(
            pipe,
            nullptr));

  auto writer =
    std::make_shared<aws::kinesis::core::IpcManager>(
        std::make_shared<aws::kinesis::core::detail::IpcChannel>(
            nullptr,
            pipe));

  const size_t total_data = 64 * 1024 * 1024;

  for (size_t size : std::vector<size_t>{{ 64, 1024, 16384, 262144 }}) {
    auto start = std::chrono::high_resolution_clock::now();

    size_t N = total_data / size;

    for (size_t i = 0; i < N; i++) {
      writer->put(std::string(size, 'a'));
    }

    std::string s;
    size_t i = 0;
    while (i < N) {
      i += reader->try_take(s) ? 1 : 0;
    }

    auto nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::high_resolution_clock::now() - start).count();
    double seconds = nanos / 1e9;
    LOG(INFO) << size << " byte messages: "
              << N / seconds << " messages per second, "
              << (double) total_data / 1024 / 1024 / seconds << " MiB/s\n";
  }

  writer->shutdown();
  reader->shutdown();
  writer->close_write_channel();
  aws::utils::sleep_for(std::chrono::milliseconds(50));
}

BOOST_AUTO_TEST_SUITE_END()
