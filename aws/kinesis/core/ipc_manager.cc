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

#include <aws/kinesis/core/ipc_manager.h>

#include <aws/utils/utils.h>

namespace aws {
namespace kinesis {
namespace core {

namespace detail {

void IpcReader::start() {
  if (channel_->open_read_channel()) {
    while (!shutdown_) {
      if (read(sizeof(len_t))) {
        len_t msg_len = 0;

        for (size_t i = 0; i < sizeof(len_t); i++) {
          int shift = (sizeof(len_t) - i - 1) * 8;
          len_t octet = (len_t) buffer_[i];
          msg_len += octet << shift;
        }

        if (msg_len > kMaxMessageSize) {
          std::stringstream ss;
          ss << "Incoming message too large, was " << msg_len
             << " bytes, max allowed is " << kMaxMessageSize << " bytes";
          throw std::runtime_error(ss.str().c_str());
        }

        if (read(msg_len)) {
          queue_->put(std::string((const char*) buffer_.data(), msg_len));
        }
      }
    }
  }
}

bool IpcReader::read(size_t len) {
  size_t read = 0;

  while (read < len) {
    auto num_read = channel_->read(buffer_.data() + read, len - read);

    if (num_read <= 0) {
      if (!shutdown_) {
        std::stringstream ss;
        if (num_read < 0) {
          ss << "IO error reading from ipc channel, errno = " << errno;
        } else if (num_read == 0) {
          ss << "EOF reached while reading from ipc channel";
        }
        throw std::runtime_error(ss.str().c_str());
      } else {
        return false;
      }
    }

    read += num_read;
  }
  return true;
}

void IpcWriter::start() {
  if (channel_->open_write_channel()) {
    std::string s;

    while (!shutdown_) {
      if (!queue_->try_take(s)) {
        aws::utils::sleep_for(std::chrono::milliseconds(5));
        continue;
      }

      for (size_t i = 0; i < sizeof(len_t); i++) {
        auto shift = (sizeof(len_t) - i - 1) * 8;
        buffer_[i] = (uint8_t)((s.length() >> shift) & 0xFF);
      }
      std::memcpy(buffer_.data() + sizeof(len_t), s.data(), s.length());

      write(sizeof(len_t) + s.length());
    }
  }
}

void IpcWriter::write(size_t len) {
  size_t wrote = 0;

  while (wrote < len) {
    auto num_written = channel_->write(buffer_.data() + wrote, len - wrote);

    if (num_written < 0) {
      if (!shutdown_) {
        std::stringstream ss;
        ss << "IO error writing to ipc channel, errno = " << errno;
        throw std::runtime_error(ss.str().c_str());
      }
      return;
    }

    wrote += num_written;
  }
}

} //namespace detail

void IpcManager::put(std::string&& data) {
  if (data.length() > kMaxMessageSize) {
    std::stringstream ss;
    ss << "Outgoing message too large, was " << data.length()
       << " bytes, max allowed is " << kMaxMessageSize << " bytes";
    throw std::runtime_error(ss.str().c_str());
  }

  out_queue_->put(std::move(data));
}

} //namespace core
} //namespace kinesis
} //namespace aws
