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
#ifdef DEBUG
  thread_local aws::auth::DebugStats debug_stats;

  void update_step(std::uint64_t& step, std::uint64_t& outcome) {
    step++;
    outcome++;
    debug_stats.attempts_++;
  }

  void update_step(std::uint64_t& step) {
    std::uint64_t ignored = 0;
    update_step(step, ignored);
  }
#else
#define update_step(...)
#endif
}

using namespace aws::auth;

MutableStaticCredentialsProvider::MutableStaticCredentialsProvider(const std::string& akid,
                                                                   const std::string& sk,
                                                                   std::string token) {
  Aws::Auth::AWSCredentials creds(akid, sk, token);
  current_.creds_ = creds;
}

void MutableStaticCredentialsProvider::set_credentials(const std::string& akid, const std::string& sk, std::string token) {
  std::lock_guard<std::mutex> lock(update_mutex_);

  //
  // The ordering of stores here is important.  current_.updating_, and current_.version_ are atomics.
  // Between the two of them they create an interlocked state change that allows consumers
  // to detect that the credentials have changed during the process of copying the values.
  //
  // Specifically current_.version_ must be incremented before current_.updating_ is set to false.
  // This ensures that consumers will either see a version mismatch or see the updating state change.
  //

  current_.updating_ = true;
  current_.creds_.SetAWSAccessKeyId(akid);
  current_.creds_.SetAWSSecretKey(sk);
  current_.creds_.SetSessionToken(token);
  current_.version_++;
  current_.updating_ = false;
}

bool MutableStaticCredentialsProvider::optimistic_read(Aws::Auth::AWSCredentials& destination) {
  //
  // This is an attempt to do an optimistic read.  We assume that the contents of the
  // credentials may change in while copying to the result.
  //
  // 1. To handle this we first check if an update is in progress, and if it is we bounce out
  // and retry.
  //
  // 2. If the credential isn't being updated we go ahead and load the current version of
  // the credential.
  //
  // 3. At this point we go ahead and trigger a copy of the credential to the result.  This
  // is what we will return if our remaining checks indicate everything is still ok.
  //
  // 4. We check again to see if the credential has entered updating, if it has it's possible
  // the version of the credential we copied was split between the two updates.  So we
  // discard our copy, and try again.
  //
  // 5. Finally we make check to see that the version hasn't changed since we started.  This
  // ensures that the credential didn't enter and exit updates between our update checks.
  //
  // If everything is ok we are safe to return the credential, while it may be a nanosecnds
  // out date it should be fine.
  //

  if (current_.updating_) {
    //
    // The credentials are currently being updated.  It's not safe to read so
    // spin while we wait for the update to complete.
    //
    update_step(debug_stats.update_before_load_, debug_stats.retried_);
    return false;
  }
  std::uint64_t starting_version = current_.version_;

  //
  // Trigger a trivial copy to the result
  //
  destination = current_.creds_;

  if (current_.updating_) {
    //
    // The credentials object is being updated and the update may have started while we were
    // copying it. For safety we discard what we have and try again.
    //
    update_step(debug_stats.update_after_load_, debug_stats.retried_);
    return false;
  }

  std::uint64_t ending_version = current_.version_;

  if (starting_version != ending_version) {
    //
    // The version changed in between the start of the copy, and the end
    // of the copy.  We can no longer trust that the resulting copy is
    // correct.  So we discard what we have and try again.
    //
    update_step(debug_stats.version_mismatch_, debug_stats.retried_);
    return false;
  }
  update_step(debug_stats.success_);
  return true;
}

Aws::Auth::AWSCredentials MutableStaticCredentialsProvider::GetAWSCredentials() {

  Aws::Auth::AWSCredentials result;

  if (!optimistic_read(result)) {
    //
    // The optimistic read failed, so just give up and use the lock to acquire the credentials.
    //
    std::lock_guard<std::mutex> lock(update_mutex_);
    update_step(debug_stats.used_lock_, debug_stats.success_);
    result = current_.creds_;
  }

  return result;
}

DebugStats MutableStaticCredentialsProvider::get_debug_stats() {
#ifdef DEBUG
  return debug_stats;
#else
  return DebugStats();
#endif
}
