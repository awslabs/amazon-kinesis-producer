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

#ifndef AWS_KINESIS_CORE_IPC_MANAGER_H_
#define AWS_KINESIS_CORE_IPC_MANAGER_H_

#include <array>
#include <thread>

#include <boost/noncopyable.hpp>
#include <boost/asio.hpp>
#include <boost/predef.h>

#include <glog/logging.h>

#include <aws/utils/io_service_executor.h>
#include <aws/utils/concurrent_linked_queue.h>

namespace aws {
namespace kinesis {
namespace core {

static constexpr const size_t kMaxMessageSize = 2 * 1024 * 1024;

namespace detail {

static constexpr const size_t kBufferSize = 2 * kMaxMessageSize;
using len_t = uint32_t;
using IpcMessageQueue = aws::utils::ConcurrentLinkedQueue<std::string>;

template <typename StreamDescriptor,
          typename NativeHandle,
          typename FileManager>
class IpcChannelImpl : public boost::noncopyable {
 public:
  IpcChannelImpl(const char* in_file, const char* out_file)
      : in_file_(in_file),
        out_file_(out_file) {}

  ~IpcChannelImpl() {
    close_write_channel();
    close_read_channel();
  }

  int64_t read(void* buf, size_t count) {
    if (!read_stream_) {
      return -1;
    }

    try {
      auto num_bytes = read_stream_->read_some(boost::asio::buffer(buf, count));
      return num_bytes;
    } catch (const boost::system::system_error& error) {
      return -1;
    }
  }

  int64_t write(void* buf, size_t count) {
    if (!write_stream_) {
      return -1;
    }

    try {
      return write_stream_->write_some(boost::asio::buffer(buf, count));
    } catch (const boost::system::system_error& error) {
      return -1;
    }
  }

  bool open_read_channel() {
    if (!in_file_) {
      return false;
    }

    if (read_stream_) {
      return true;
    }

    in_handle_ = FileManager::open_read(in_file_);
    read_stream_ = std::make_unique<StreamDescriptor>(io_service_, in_handle_);
    return true;
  }

  bool open_write_channel() {
    if (!out_file_) {
      return false;
    }

    if (write_stream_) {
      return true;
    }

    out_handle_ = FileManager::open_write(out_file_);
    write_stream_ =
        std::make_unique<StreamDescriptor>(io_service_, out_handle_);
    return true;
  }

  void close_read_channel() {
    if (read_stream_) {
      read_stream_->close();
    }
    FileManager::close_read(in_handle_);
  }

  void close_write_channel() {
    if (write_stream_) {
      write_stream_->close();
    }
    FileManager::close_write(out_handle_);
  }

 private:
  const char* in_file_;
  const char* out_file_;
  NativeHandle in_handle_;
  NativeHandle out_handle_;
  std::unique_ptr<StreamDescriptor> read_stream_;
  std::unique_ptr<StreamDescriptor> write_stream_;
  boost::asio::io_service io_service_;
};

#if BOOST_OS_WINDOWS

#include <windows.h>

constexpr const char* kPipePrefix = "\\\\.\\pipe\\";

struct WindowsPipeManager {
  static HANDLE open_read(const char* f) {
    std::string name = kPipePrefix;
    name += f;
    return CreateNamedPipe(name.c_str(),
                           PIPE_ACCESS_INBOUND,
                           PIPE_TYPE_BYTE,
                           1,
                           0,
                           8192,
                           0,
                           nullptr);
  }

  static void close_read(HANDLE handle) {
    CloseHandle(handle);
  }

  static HANDLE open_write(const char* f) {
    std::string name = kPipePrefix;
    name += f;
    return CreateNamedPipe(name.c_str(),
                           PIPE_ACCESS_OUTBOUND,
                           PIPE_TYPE_BYTE,
                           1,
                           8192,
                           0,
                           0,
                           nullptr);
  }

  static void close_write(HANDLE handle) {
    CloseHandle(handle);
  }
};

using IpcChannel = IpcChannelImpl<boost::asio::windows::stream_handle,
                                  HANDLE,
                                  WindowsPipeManager>;

#else

struct PosixFileManager {
  static int open_read(const char* f) {
    return ::open(f, O_RDONLY);
  }

  static void close_read(int fd) {
    ::close(fd);
  }

  static int open_write(const char* f) {
    return ::open(f, O_WRONLY);
  }

  static void close_write(int fd) {
    ::close(fd);
  }
};

using IpcChannel = IpcChannelImpl<boost::asio::posix::stream_descriptor,
                                  int,
                                  PosixFileManager>;

#endif

class IpcWorker : public boost::noncopyable {
 public:
  IpcWorker(std::shared_ptr<IpcChannel> channel,
            std::shared_ptr<IpcMessageQueue> queue)
      : channel_(std::move(channel)),
        queue_(std::move(queue)),
        shutdown_(false) {}

  virtual ~IpcWorker() {
    shutdown();
  }

  virtual void shutdown() {
    shutdown_ = true;
  }

  virtual void start() = 0;

 protected:
  std::shared_ptr<IpcChannel> channel_;
  std::shared_ptr<IpcMessageQueue> queue_;
  bool shutdown_;
  std::array<uint8_t, kBufferSize> buffer_;
};

class IpcReader : public IpcWorker {
 public:
  IpcReader(std::shared_ptr<IpcChannel> channel,
            std::shared_ptr<IpcMessageQueue> queue)
      : IpcWorker(std::move(channel), std::move(queue)) {}

  void start() override;

 private:
  bool read(size_t len);
};

class IpcWriter : public IpcWorker {
 public:
  IpcWriter(std::shared_ptr<IpcChannel> channel,
            std::shared_ptr<IpcMessageQueue> queue)
      : IpcWorker(std::move(channel), std::move(queue)) {}

  void start() override;

 private:
  void write(size_t len);
};

} // namespace detail

class IpcManager : boost::noncopyable {
 public:
  IpcManager(
    const std::shared_ptr<detail::IpcChannel>& channel)
      : in_queue_(std::make_shared<detail::IpcMessageQueue>()),
        out_queue_(std::make_shared<detail::IpcMessageQueue>()),
        executor_(2),
        channel_(channel),
        reader_(std::make_unique<detail::IpcReader>(channel_, in_queue_)),
        writer_(std::make_unique<detail::IpcWriter>(channel_, out_queue_)) {
    executor_.submit([this] { reader_->start(); });
    executor_.submit([this] { writer_->start(); });
  }

  ~IpcManager() {
    shutdown();
    close_write_channel();
    close_read_channel();
  }

  void put(std::string&& data);

  bool try_take(std::string& dest) {
    return in_queue_->try_take(dest);
  }

  void shutdown() {
    reader_->shutdown();
    writer_->shutdown();
  }

  void close_write_channel() {
    channel_->close_write_channel();
  }

  void close_read_channel() {
    channel_->close_read_channel();
  }

 private:
  std::shared_ptr<detail::IpcMessageQueue> in_queue_;
  std::shared_ptr<detail::IpcMessageQueue> out_queue_;
  aws::utils::IoServiceExecutor executor_;
  std::shared_ptr<detail::IpcChannel> channel_;
  std::unique_ptr<detail::IpcWorker> reader_;
  std::unique_ptr<detail::IpcWorker> writer_;
};

} //namespace core
} //namespace kinesis
} //namespace aws

#endif //AWS_KINESIS_CORE_IPC_MANAGER_H_
