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

#include "mutable_static_creds_provider.h"

namespace {
  //
  // Provides a thread scoped copy of the current credentials to an executing thread.
  // This makes the most difference when using a thread pool, as the retrieval of the
  // credentials will only require a lock when the credentials version changes.
  //
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

  //
  // Since the credentials are created with the expected next version, and the entire update
  // is protected by a lock we can't get into a scenario where one of the consumers has
  // a mismatched version and credentials.  
  //
  std::shared_ptr<VersionedCredentials> new_credentials = std::make_shared<VersionedCredentials>(next_version, akid, sk, token);

  //
  // This update the credentials atomically using the shared_ptr specific atomic operations,
  // and doesn't require a specific lock on the shared_ptr during the update.  The lock
  // taken previously is to prevent two credential updates at the same time.
  //
  // The global version change allows the threads to detect the updated version.  Once detected
  // the threads will pull the updated credential to their own thread local copy.  
  //
  std::atomic_store(&creds_, new_credentials);
  version_ = next_version;
}

Aws::Auth::AWSCredentials MutableStaticCredentialsProvider::GetAWSCredentials() {
  //
  // Check to see if the credentials have been updated.  If they have load the credentials
  // and update the thread local.
  //
  // If the credentials are changing rapidly it's possible that the thread will read an
  // old version of the credentials.  Should that occur the next read will update to the
  // most current version.
  //
  // This check still works in the very unlikely event that the next_version value
  // wraps around.  
  //
  if (current_creds.version_ != version_) {
    std::shared_ptr<VersionedCredentials> updated = std::atomic_load(&creds_);
    current_creds = *updated;
  }
  return current_creds.creds_;
}
