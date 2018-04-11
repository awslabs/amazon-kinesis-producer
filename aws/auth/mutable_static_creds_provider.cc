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
  creds_ = std::make_shared<Aws::Auth::AWSCredentials>(akid, sk, token);
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

  std::shared_ptr<Aws::Auth::AWSCredentials> new_credentials = std::make_shared<Aws::Auth::AWSCredentials>(akid, sk, token);

  std::atomic_store(&creds_, new_credentials);
}

Aws::Auth::AWSCredentials MutableStaticCredentialsProvider::GetAWSCredentials() {
  std::shared_ptr<Aws::Auth::AWSCredentials> result = std::atomic_load(&creds_);
  return *result;
}

DebugStats MutableStaticCredentialsProvider::get_debug_stats() {
#ifdef DEBUG
  return debug_stats;
#else
  return DebugStats();
#endif
}
