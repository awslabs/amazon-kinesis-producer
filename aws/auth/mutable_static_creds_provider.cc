// Copyright 2018 Amazon.com, Inc. or its affiliates. All Rights Reserved.
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

#include "mutable_static_creds_provider.h"
#include <limits>

namespace {
  thread_local aws::auth::VersionedCredentials current_creds;
}

using namespace aws::auth;

VersionedCredentials::VersionedCredentials(std::uint64_t version, const std::string& akid, const std::string& sk, const std::string& token) :
  version_(version), creds_(Aws::Auth::AWSCredentials(akid, sk, token)) {
}

MutableStaticCredentialsProvider::MutableStaticCredentialsProvider(const std::string& akid,
                                                                   const std::string& sk,
                                                                   std::string token) :
  creds_(std::make_shared<VersionedCredentials>(1, akid, sk, token)), version_(1) {
}

void MutableStaticCredentialsProvider::set_credentials(const std::string& akid, const std::string& sk, std::string token) {
  std::lock_guard<std::mutex> lock(update_mutex_);

  std::uint64_t next_version = version_ + 1;
  

  std::shared_ptr<VersionedCredentials> new_credentials = std::make_shared<VersionedCredentials>(next_version, akid, sk, token);
  std::atomic_store(&creds_, new_credentials);
  version_ = next_version;
}

Aws::Auth::AWSCredentials MutableStaticCredentialsProvider::GetAWSCredentials() {
  if (current_creds.version_ != version_) {
    std::shared_ptr<VersionedCredentials> updated = std::atomic_load(&creds_);
    current_creds = *updated;
  }
  return current_creds.creds_;
}
