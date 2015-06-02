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

#include <aws/http/ec2_metadata.h>

#include <aws/mutex.h>

namespace aws {
namespace http {

const std::regex Ec2Metadata::kRegionFromAzRegex{
    "^([a-z]+-[a-z]+-[0-9])[a-z]$",
    std::regex::ECMAScript | std::regex::optimize};

const std::chrono::milliseconds Ec2Metadata::kTimeout{100};

void Ec2Metadata::get(std::string path, const Callback& callback) {
  auto done = std::make_shared<std::atomic<bool>>(false);

  executor_->schedule(
      [=] {
        if (!done->exchange(true)) {
          callback(false, "Timed out");
        }
      },
      kTimeout);

  HttpRequest req("GET", path);
  http_client_.put(req, [=](auto& result) {
    if (!done->exchange(true)) {
      callback(result->successful(),
               result->successful()
                  ? result->response_body()
                  : result->error());
    }
  });
}

void Ec2Metadata::get_az(const Callback& callback) {
  get("/latest/meta-data/placement/availability-zone/", callback);
}

boost::optional<std::string> Ec2Metadata::get_region() {
  bool done = false;
  boost::optional<std::string> region;
  aws::condition_variable cv;
  aws::mutex mutex;
  aws::unique_lock<aws::mutex> lock(mutex);
  get_region([&](bool success, auto& result) {
    aws::unique_lock<aws::mutex> lock(mutex);
    if (success) {
      region = result;
    }
    done = true;
    cv.notify_one();
  });
  cv.wait(lock, [&] { return done; });
  return region;
}

void Ec2Metadata::get_region(const Callback& callback) {
  get_az([=](bool success, auto& result) {
    if (!success) {
      callback(false, result);
      return;
    }

    std::smatch m;
    if (std::regex_match(result,
                         m,
                         kRegionFromAzRegex)) {
      callback(true, m.str(1));
    } else {
      callback(
          false,
          std::string(
              "Could not read region, got unexpected result ") + result);
    }
  });
}

void Ec2Metadata::get_instance_profile_name(const Callback& callback) {
  get(
      "/latest/meta-data/iam/security-credentials/",
      [=](bool success, auto& result) {
    if (!success) {
      callback(false, result);
      return;
    }

    try {
      callback(true, get_first_line(result));
    } catch (const std::exception& ex) {
      callback(false, ex.what());
    }
  });
}

void Ec2Metadata::get_instance_profile_credentials(const Callback& callback) {
  get_instance_profile_name([=](bool success, auto& result) {
    if (!success) {
      callback(false, result);
      return;
    }

    auto path = "/latest/meta-data/iam/security-credentials/" + result + "/";
    this->get(path, callback);
  });
}

// static
std::string Ec2Metadata::get_first_line(const std::string& s) {
  std::vector<std::string> v;
  boost::split(v,
               s,
               boost::is_any_of("\r\n"),
               boost::algorithm::token_compress_on);
  return v.at(0);
}

} //namespace http
} //namespace aws*/
