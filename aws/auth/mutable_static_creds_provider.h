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
#include <chrono>
#include <condition_variable>
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

  // Starts without credentials. Once enable_first_credentials_wait() has been called,
  // GetAWSCredentials() waits for the first set_credentials() call, for at most
  // first_credentials_timeout, instead of returning empty credentials.
  explicit MutableStaticCredentialsProvider(std::chrono::milliseconds first_credentials_timeout);

  void set_credentials(const std::string& akid, const std::string& sk, std::string token = "");

  // Makes GetAWSCredentials() wait for the first credentials, unless they have already
  // arrived. Call it only once credentials can arrive: the AWS SDK reads credentials
  // while it builds a client, and a wait there lasts the whole timeout.
  void enable_first_credentials_wait();

  Aws::Auth::AWSCredentials GetAWSCredentials() override;

 private:
  void wait_for_first_credentials();

  std::mutex update_mutex_;
  std::shared_ptr<VersionedCredentials> creds_;
  std::atomic<std::uint64_t> version_;
  std::atomic<bool> awaiting_first_credentials_;
  // Guarded by update_mutex_
  bool has_credentials_;
  std::chrono::milliseconds first_credentials_timeout_;
  std::condition_variable first_credentials_cv_;

};

} //namespace auth
} //namespace aws

#endif //AWS_AUTH_MUTABLE_STATIC_CREDS_PROVIDER_H_
