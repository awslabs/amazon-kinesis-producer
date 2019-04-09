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

#include <boost/test/unit_test.hpp>

#include <aws/kinesis/core/test/test_utils.h>
#include <aws/kinesis/core/ipc_manager.h>
#include <aws/utils/utils.h>

namespace {

class Wrapper {
 public:
  Wrapper() {
    reader_ =
        std::make_unique<aws::kinesis::core::IpcManager>(
            std::make_shared<aws::kinesis::core::detail::IpcChannel>(
                pipe_,
                nullptr));
    aws::utils::sleep_for(std::chrono::milliseconds(100));
    writer_ =
        std::make_unique<aws::kinesis::core::IpcManager>(
            std::make_shared<aws::kinesis::core::detail::IpcChannel>(
                nullptr,
                pipe_,
                false));
  }

  ~Wrapper() {
    writer_->shutdown();
    reader_->shutdown();
    writer_->close_write_channel();
    aws::utils::sleep_for(std::chrono::milliseconds(100));
  }

  aws::kinesis::core::IpcManager& reader() {
    return *reader_;
  }

  aws::kinesis::core::IpcManager& writer() {
    return *writer_;
  }

  void put(std::string&& msg) {
    writer_->put(std::move(msg));
  }

  bool try_take(std::string& target) {
    return reader_->try_take(target);
  }

 private:
  aws::kinesis::test::Fifo pipe_;
  std::unique_ptr<aws::kinesis::core::IpcManager> reader_;
  std::unique_ptr<aws::kinesis::core::IpcManager> writer_;
};

} //namespace

BOOST_AUTO_TEST_SUITE(IpcManager)

// We hook two IpcManagers up, sending data from one to the other. We check that
// the data read is the same as the data wrote. We test different data sizes,
// including those near and beyond the allowed range.
BOOST_AUTO_TEST_CASE(DataIntegrity) {
  Wrapper wrapper;

  std::vector<std::string> read;
  std::vector<std::string> wrote;

  auto f = [&](size_t sz) {
    std::string data = aws::kinesis::test::random_string(sz);
    std::string data_copy = data;
    wrapper.put(std::move(data));
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
  while (wrapper.try_take(s)) {
    read.push_back(s);
  }

  BOOST_CHECK_EQUAL(read.size(), wrote.size());
  for (size_t i = 0; i < read.size(); i++) {
    BOOST_CHECK_EQUAL(read[i], wrote[i]);
  }
}

// Measure how many pairs of messages can be read/written per second.
BOOST_AUTO_TEST_CASE(Throughput) {
  Wrapper wrapper;

  const size_t total_data = 64 * 1024 * 1024;

  for (size_t size : std::vector<size_t>{{ 64, 1024, 16384, 262144 }}) {
    auto start = std::chrono::high_resolution_clock::now();

    size_t N = total_data / size;

    for (size_t i = 0; i < N; i++) {
      wrapper.put(std::string(size, 'a'));
    }

    std::string s;
    size_t i = 0;
    while (i < N) {
      i += wrapper.try_take(s) ? 1 : 0;
    }

    double seconds = aws::utils::seconds_since(start);
    LOG(info) << size << " byte messages: "
              << N / seconds << " messages per second, "
              << (double) total_data / 1024 / 1024 / seconds << " MiB/s";
  }
}

BOOST_AUTO_TEST_SUITE_END()
