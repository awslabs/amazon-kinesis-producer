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

#ifndef AWS_HTTP_EC2_METADATA_H_
#define AWS_HTTP_EC2_METADATA_H_

#include <array>
#include <condition_variable>
#include <regex>
#include <string>
#include <thread>

#include <boost/algorithm/string/trim.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/noncopyable.hpp>
#include <boost/optional.hpp>

#include <aws/http/http_client.h>
#include <aws/utils/executor.h>

namespace aws {
namespace http {

class Ec2Metadata : boost::noncopyable {
 public:
  using Callback = std::function<void (bool, const std::string&)>;

  Ec2Metadata(const std::shared_ptr<aws::utils::Executor>& executor,
              const std::shared_ptr<aws::http::SocketFactory>& socket_factory,
              std::string ip = "169.254.169.254",
              uint16_t port = 80,
              bool secure = false)
      : executor_(executor),
        http_client_(executor,
                     socket_factory,
                     ip,
                     port,
                     secure,
                     false,
                     1,
                     1,
                     std::chrono::milliseconds(500),
                     std::chrono::milliseconds(500),
                     true) {}

  void get(std::string path, const Callback& callback);

  void get_az(const Callback& callback);

  boost::optional<std::string> get_region();

  void get_region(const Callback& callback);

  void get_instance_profile_name(const Callback& callback);

  void get_instance_profile_credentials(const Callback& callback);

 private:
  static const std::chrono::milliseconds kTimeout;
  static const std::regex kRegionFromAzRegex;

  static std::string get_first_line(const std::string& s);

  std::shared_ptr<aws::utils::Executor> executor_;
  aws::http::HttpClient http_client_;
};

} //namespace http
} //namespace aws

#endif //AWS_HTTP_EC2_METADATA_H_
