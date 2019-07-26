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

#ifndef AWS_AUTH_MUTABLE_STATIC_CREDS_PROVIDER_H_
#define AWS_AUTH_MUTABLE_STATIC_CREDS_PROVIDER_H_

#include <aws/core/auth/AWSCredentialsProvider.h>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <memory>

namespace aws {
namespace auth {

struct VersionedCredentials {
  std::uint64_t version_;
  Aws::Auth::AWSCredentials creds_;

  VersionedCredentials() : version_(0), creds_("", "", "") {}
  VersionedCredentials(std::uint64_t version, const std::string& akid, const std::string& sk, const std::string& token);
};

// Like basic static creds, but with an atomic set operation
class MutableStaticCredentialsProvider
    : public Aws::Auth::AWSCredentialsProvider {
 public:
  MutableStaticCredentialsProvider(const std::string& akid, const std::string& sk, std::string token = "");

  void set_credentials(const std::string& akid, const std::string& sk, std::string token = "");

  Aws::Auth::AWSCredentials GetAWSCredentials() override;

 private:
  std::mutex update_mutex_;
  std::shared_ptr<VersionedCredentials> creds_;
  std::atomic<std::uint64_t> version_;

};

} //namespace auth
} //namespace aws

#endif //AWS_AUTH_MUTABLE_STATIC_CREDS_PROVIDER_H_
