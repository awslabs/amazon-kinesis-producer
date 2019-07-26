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

#include <aws/kinesis/core/test/test_utils.h>

#include <boost/test/unit_test.hpp>

namespace aws {
namespace kinesis {
namespace test {

std::shared_ptr<aws::kinesis::core::UserRecord>
make_user_record(const std::string& partition_key,
                 const std::string& data,
                 const std::string& explicit_hash_key,
                 uint64_t deadline,
                 const std::string& stream,
                 uint64_t source_id) {
  aws::kinesis::protobuf::Message m;
  m.set_id(source_id);
  auto put_record = m.mutable_put_record();
  put_record->set_partition_key(partition_key);
  if (!explicit_hash_key.empty()) {
    put_record->set_explicit_hash_key(explicit_hash_key);
  }
  put_record->set_data(data);
  put_record->set_stream_name(stream);
  auto r = std::make_shared<aws::kinesis::core::UserRecord>(m);
  r->set_deadline_from_now(std::chrono::milliseconds(deadline));
  r->set_expiration_from_now(std::chrono::milliseconds(deadline * 2));
  return r;
}

Fifo::Fifo() {
  auto ts = std::chrono::steady_clock::now().time_since_epoch().count();
#if !BOOST_OS_WINDOWS
  name_ = "__test_fifo_" + std::to_string(ts) + "_delete_me";
  std::string cmd = "mkfifo ";
  cmd += name_;
  std::system(cmd.c_str());
#else
  name_ = "\\\\.\\pipe\\__test_fifo_" + std::to_string(ts) + "_delete_me";
#endif
}

Fifo::~Fifo() {
  std::remove(name_.c_str());
}

Fifo::operator const char*() const {
  return name_.c_str();
}

// Use only printable chars so we can look at it on the console if tests fail
std::string random_string(size_t len) {
  std::string s;
  s.reserve(len);
  for (size_t i = 0; i < len; i++) {
    s += (char) (33 + (std::rand() % 93));
  }
  return s;
}

void verify_format(const UserRecordSharedPtrVector& original,
                   aws::kinesis::core::KinesisRecord& kr,
                   aws::kinesis::protobuf::AggregatedRecord& container) {
  std::string serialized = kr.serialize();

  // verify magic number
  std::string expected_magic(aws::kinesis::core::KinesisRecord::kMagic);
  size_t magic_len = expected_magic.length();
  std::string magic = serialized.substr(0, magic_len);
  BOOST_CHECK_EQUAL(expected_magic, magic);

  // verify protobuf payload
  std::string payload = serialized.substr(
      expected_magic.length(),
      serialized.length() - 16 - magic_len);
  BOOST_CHECK_MESSAGE(container.ParseFromString(payload),
                      "AggregatedRecord should've deserialized successfully");

  // verify md5 checksum
  BOOST_CHECK_EQUAL(aws::utils::md5(payload),
                    serialized.substr(serialized.length() - 16, 16));

  // verify the explicit hash key set on the Kinesis record
  std::unordered_set<std::string> acceptable_hash_keys;
  for (const auto& ur : original) {
    if (ur->explicit_hash_key()) {
      acceptable_hash_keys.emplace(ur->explicit_hash_key().get());
    } else {
      acceptable_hash_keys.emplace(
          aws::utils::md5_decimal(ur->partition_key()));
    }
  }

  BOOST_CHECK_MESSAGE(
      acceptable_hash_keys.find(kr.explicit_hash_key()) !=
          acceptable_hash_keys.end(),
      "Kinesis record should've used a valid hash key");
}

void verify_content(const UserRecordSharedPtrVector& original,
                    const aws::kinesis::protobuf::AggregatedRecord& result) {
  // verify record count
  BOOST_CHECK_EQUAL(original.size(), result.records_size());

  for (int i = 0; i < result.records_size(); i++) {
    auto r = result.records(i);
    // verify partition key
    BOOST_CHECK_EQUAL(original[i]->partition_key(),
                      result.partition_key_table(r.partition_key_index()));

    // verify explicit hash key
    if (original[i]->explicit_hash_key()) {
      BOOST_CHECK_EQUAL(
          original[i]->explicit_hash_key().get(),
          result.explicit_hash_key_table(r.explicit_hash_key_index()));
    }

    // verify data
    BOOST_CHECK_EQUAL(original[i]->data(), r.data());
  }
}

void verify(const UserRecordSharedPtrVector& original,
            aws::kinesis::core::KinesisRecord& kr) {
  aws::kinesis::protobuf::AggregatedRecord ar;
  verify_format(original, kr, ar);
  verify_content(original, ar);
}

void verify_unaggregated(
    const std::shared_ptr<aws::kinesis::core::UserRecord>& ur,
    aws::kinesis::core::KinesisRecord& kr) {
  auto serialized = kr.serialize();
  BOOST_CHECK_EQUAL(ur->data(), serialized);
  BOOST_CHECK_EQUAL(ur->partition_key(), kr.partition_key());
  if (ur->explicit_hash_key()) {
    BOOST_CHECK_EQUAL(ur->explicit_hash_key().get(), kr.explicit_hash_key());
  } else {
    BOOST_CHECK_EQUAL(aws::utils::md5_decimal(ur->partition_key()),
                      kr.explicit_hash_key());
  }
}

} //namespace test
} //namespace kinesis
} //namespace aws
