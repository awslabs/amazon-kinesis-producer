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

#include <aws/kinesis/core/user_record.h>

#include <aws/utils/logging.h>

namespace aws {
namespace kinesis {
namespace core {

UserRecord::UserRecord(aws::kinesis::protobuf::Message& m)
    : hash_key_(0),
      finished_(false) {
  if (!m.has_put_record()) {
    throw std::runtime_error("Message is not a PutRecord");
  }

  source_id_ = m.id();
  auto put_record = m.put_record();
  stream_ = std::move(put_record.stream_name());
  partition_key_ = std::move(put_record.partition_key());
  data_ = std::move(put_record.data());
  has_explicit_hash_key_ = put_record.has_explicit_hash_key();

  if (has_explicit_hash_key_) {
    hash_key_ = uint128_t(put_record.explicit_hash_key());
  } else {
    auto digest = aws::utils::md5_binary(partition_key_);
    for (int i = 0; i < 16; i++) {
      uint128_t p(digest[i]);
      p <<= (16 - i - 1) * 8;
      hash_key_ += p;
    }
  }
}

aws::kinesis::protobuf::Message UserRecord::to_put_record_result() {
  aws::kinesis::protobuf::Message m;
  m.set_source_id(source_id_);
  m.set_id(::rand());

  auto prr = m.mutable_put_record_result();
  prr->set_success(false);

  prr->set_stream_name(std::move(stream_));
  prr->set_partition_key(std::move(partition_key_));
  if (has_explicit_hash_key_) {
    prr->set_explicit_hash_key(std::move(hash_key_.str()));
  }
  prr->set_data(std::move(data_));

  for (size_t i = 0; i < attempts_.size(); i++) {
    auto a = prr->add_attempts();
    auto delay = (i == 0)
        ? attempts_[i].start() - this->arrival()
        : attempts_[i].start() - attempts_[i - 1].end();
    a->set_delay(
        std::chrono::duration_cast<std::chrono::milliseconds>(delay).count());
    a->set_duration(attempts_[i].duration().count());
    a->set_success(attempts_[i]);

    if (attempts_[i]) {
      prr->set_shard_id(std::move(attempts_[i].shard_id()));
      prr->set_sequence_number(std::move(attempts_[i].sequence_number()));
      prr->set_success(true);
    } else {
      a->set_error_code(std::move(attempts_[i].error_code()));
      a->set_error_message(std::move(attempts_[i].error_message()));
    }
  }

  finished_ = true;
  return m;
}

} //namespace core
} //namespace kinesis
} //namespace aws
