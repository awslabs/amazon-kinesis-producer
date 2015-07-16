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

#ifndef AWS_HTTP_SOCKET_H_
#define AWS_HTTP_SOCKET_H_

#include <chrono>
#include <functional>
#include <memory>
#include <string>

#include <boost/noncopyable.hpp>

namespace aws {
namespace http {

class Socket : boost::noncopyable {
 public:
  using ConnectCallback = std::function<void (bool, const std::string&)>;
  using WriteCallback = std::function<void (bool, const std::string&)>;
  using ReadCallback = std::function<void (int, const std::string&)>;

  virtual void open(const ConnectCallback& cb,
                    std::chrono::milliseconds timeout) = 0;

  virtual void write(const char* data,
                     size_t len,
                     const WriteCallback& cb,
                     std::chrono::milliseconds timeout) = 0;

  virtual void read(char* dest,
                    size_t max_len,
                    const ReadCallback& cb,
                    std::chrono::milliseconds timeout) = 0;

  virtual bool good() = 0;

  virtual void close() = 0;

  operator bool() {
    return good();
  }
};

class SocketFactory : boost::noncopyable {
 public:
  virtual std::shared_ptr<Socket> create(const std::string& endpoint,
                                         int port,
                                         bool secure,
                                         bool verify_cert) = 0;
};

} //namespace http
} //namespace aws

#endif //AWS_HTTP_SOCKET_H_
