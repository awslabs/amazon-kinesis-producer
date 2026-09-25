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

#include <aws/utils/logging.h>

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
  creds_(std::make_shared<VersionedCredentials>(1, akid, sk, token)), version_(1),
  awaiting_first_credentials_(false), first_credentials_timeout_(0) {
}

MutableStaticCredentialsProvider::MutableStaticCredentialsProvider(
    std::chrono::milliseconds first_credentials_timeout) :
  creds_(std::make_shared<VersionedCredentials>(1, "", "", "")), version_(1),
  awaiting_first_credentials_(true), first_credentials_timeout_(first_credentials_timeout) {
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

  //
  // Wake any request that is waiting for the first credentials. The flag changes while
  // update_mutex_ is held, which is the mutex the waiters use, so no wakeup is lost.
  //
  if (awaiting_first_credentials_) {
    awaiting_first_credentials_ = false;
    first_credentials_cv_.notify_all();
  }
}

void MutableStaticCredentialsProvider::wait_for_first_credentials() {
  std::unique_lock<std::mutex> lock(update_mutex_);
  bool done_waiting = first_credentials_cv_.wait_for(lock, first_credentials_timeout_, [this] {
      return !awaiting_first_credentials_;
    });
  if (done_waiting) {
    return;
  }

  //
  // Wait only once. A producer that never receives credentials should not stall every
  // request; after the timeout it behaves as it did before this wait existed.
  //
  awaiting_first_credentials_ = false;
  LOG(warning) << "No credentials received from the Java process within "
               << first_credentials_timeout_.count()
               << " ms; requests are sent unsigned until credentials arrive";
}

Aws::Auth::AWSCredentials MutableStaticCredentialsProvider::GetAWSCredentials() {
  if (awaiting_first_credentials_) {
    wait_for_first_credentials();
  }
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
