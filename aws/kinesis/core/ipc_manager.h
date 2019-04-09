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

#ifndef AWS_KINESIS_CORE_IPC_MANAGER_H_
#define AWS_KINESIS_CORE_IPC_MANAGER_H_

#include <array>
#include <thread>
#include <cstring>

#include <boost/noncopyable.hpp>
#include <boost/asio.hpp>
#include <boost/predef.h>

#include <aws/utils/logging.h>

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
  IpcChannelImpl(std::string& in_file,
                 std::string& out_file,
                 bool create_pipes = true)
      : IpcChannelImpl(in_file.c_str(), out_file.c_str(), create_pipes)
  {}


  IpcChannelImpl(const char* in_file,
                 const char* out_file,
                 bool create_pipes = true) :
      create_pipes_(create_pipes),
      in_handle_(0),
      out_handle_(0)
  {
      if (in_file) {
          size_t in_file_len = std::strlen(in_file) + 1;
          in_file_ = new char[in_file_len];
          std::strncpy(in_file_, in_file, in_file_len);
      } else {
          in_file_ = nullptr;
      }

      if (out_file) {
          size_t out_file_len = std::strlen(out_file) + 1;
          out_file_ = new char[out_file_len];
          std::strncpy(out_file_, out_file, out_file_len);
      } else {
          out_file_ = nullptr;
      }
  }



  ~IpcChannelImpl() {
    close_write_channel();
    close_read_channel();    
    if (in_file_) {
        delete [] in_file_;
    }
    if (out_file_) {
        delete [] out_file_;
    }
  }

  int64_t read(void* buf, size_t count) {
    if (in_handle_ == 0) {
      return -1;
    }
    return FileManager::read(in_handle_, buf, count);
  }

  int64_t write(void* buf, size_t count) {
    if (out_handle_ == 0) {
      return -1;
    }
    return FileManager::write(out_handle_, buf, count);
  }

  bool open_read_channel() {
    if (!in_file_) {
      return false;
    }
    in_handle_ = FileManager::open_read(in_file_, create_pipes_);
    return true;
  }

  bool open_write_channel() {
    if (!out_file_) {
      return false;
    }
    out_handle_ = FileManager::open_write(out_file_, create_pipes_);
    return true;
  }

  void close_read_channel() {
    if (in_handle_ != 0) {
      FileManager::close_read(in_handle_);
      in_handle_ = 0;
    }
  }

  void close_write_channel() {
    if (out_handle_ != 0) {
      FileManager::close_write(out_handle_);
      out_handle_ = 0;
    }
  }

 private:
  char* in_file_;
  char* out_file_;
  bool create_pipes_;
  NativeHandle in_handle_;
  NativeHandle out_handle_;
};

#if BOOST_OS_WINDOWS

#include <windows.h>

struct WindowsPipeManager {
  static HANDLE open_read(const char* f, bool create_pipe) {
    return open(f, false, create_pipe);
  }

  static void close_read(HANDLE handle) {
    close(handle);
  }

  static HANDLE open_write(const char* f, bool create_pipe) {
    return open(f, true, create_pipe);
  }

  static void close_write(HANDLE handle) {
    close(handle);
  }

  static HANDLE open(const char* f, bool write, bool create_pipe) {
    if (create_pipe) {
      auto handle =
          CreateNamedPipe(
              f,
              write ? PIPE_ACCESS_OUTBOUND : PIPE_ACCESS_INBOUND,
              PIPE_TYPE_BYTE,
              1,
              8192,
              8192,
              0,
              nullptr);

      if (handle == INVALID_HANDLE_VALUE) {
        error("Could not create pipe", f);
      }

      if (!ConnectNamedPipe(handle, nullptr)) {
        error("Could not connect pipe", f);
      }

      return handle;
    } else {
      auto handle = CreateFile(f,
                               write ? GENERIC_WRITE : GENERIC_READ,
                               0,
                               nullptr,
                               OPEN_EXISTING,
                               FILE_ATTRIBUTE_NORMAL,
                               nullptr);

      if (handle == INVALID_HANDLE_VALUE) {
        error("Could not open file", f);
      }

      return handle;
    }
  }

  static int64_t read(HANDLE handle, void* buf, size_t capacity) {
    unsigned long int num_written = 0;
    bool ok = ReadFile(handle, buf, capacity, &num_written, nullptr);
    if (ok) {
      return num_written;
    } else {
      return -1;
    }
  }

  static int64_t write(HANDLE handle, const void* buf, size_t len) {
    unsigned long int num_written = 0;
    bool ok = WriteFile(handle, buf, len, &num_written, nullptr);
    if (ok) {
      return num_written;
    } else {
      return -1;
    }
  }

  static error(const char* msg, const char* fname) {
    std::stringstream ss;
    ss << msg << " \"" << fname << "\", error code " << GetLastError();
    throw std::runtime_error(ss.str().c_str());
  }

  static void close(HANDLE handle) {
    FlushFileBuffers(handle);
    DisconnectNamedPipe(handle);
    CloseHandle(handle);
  }
};

using IpcChannel = IpcChannelImpl<boost::asio::windows::stream_handle,
                                  HANDLE,
                                  WindowsPipeManager>;

#else

struct PosixFileManager {
  static int open_read(const char* f, bool /*create_pipe*/) {
    return open(f, false);
  }

  static void close_read(int fd) {
    ::close(fd);
  }

  static int open_write(const char* f, bool /*create_pipe*/) {
    return open(f, true);
  }

  static void close_write(int fd) {
    ::close(fd);
  }

  static int open(const char* f, bool write) {
    auto res = ::open(f, write ? O_WRONLY : O_RDONLY);
    if (res <= 0) {
      error("Could not open file", f);
    }
    return res;
  }

  static int64_t read(int fd, void* buf, size_t capacity) {
    return ::read(fd, buf, capacity);
  }

  static int64_t write(int fd, const void* buf, size_t len) {
    return ::write(fd, buf, len);
  }

  static void error(const char* msg, const char* fname) {
    std::stringstream ss;
    ss << msg << " \"" << fname << "\", error code " << errno;
    throw std::runtime_error(ss.str().c_str());
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
