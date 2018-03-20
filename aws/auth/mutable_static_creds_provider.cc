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
  thread_local Aws::Auth::AWSCredentials* current_credentials = nullptr;
}

using namespace aws::auth;

MutableStaticCredentialsProvider::MutableStaticCredentialsProvider(const std::string& akid,
                                                                   const std::string& sk,
                                                                   std::string token)
  : current_slot_(0) {
  Aws::Auth::AWSCredentials creds(akid, sk, token);
  
  slots_[0].creds_ = creds;
    
}

void MutableStaticCredentialsProvider::set_credentials(const std::string& akid, const std::string& sk, std::string token) {
  std::lock_guard<std::mutex> lock(update_mutex_);

  std::size_t next_slot = (current_slot_ + 1) % slots_.size();

  VersionedCredentials* next_creds = &slots_[next_slot];
  next_creds->version_++;
  next_creds->creds_.SetAWSAccessKeyId(akid);
  next_creds->version_++;
  next_creds->creds_.SetAWSSecretKey(sk);
  next_creds->version_++;
  next_creds->creds_.SetSessionToken(token);
  next_creds->version_++;

  current_slot_ = next_slot;
  
}

Aws::Auth::AWSCredentials MutableStaticCredentialsProvider::GetAWSCredentials() {


  Aws::Auth::AWSCredentials result;
  //
  // This is an attempt to do an optimistic read.  We assume that the contents of the
  // credentials are unlikely to change in between a read especially since the slots
  // constantly move forward.  So we start to read, and after the read see if the
  // version changed.  Versions advance at the start of the modification, and after each
  // mutation step.  If we see a version mismatch we try again.  This shouldn't hit
  // that often, since cereds don't change that much.
  //

  std::uint64_t starting_version = 0, ending_version = 0;
  do {
      std::size_t slot = current_slot_.load();

      VersionedCredentials* creds = &slots_[slot];
      std::uint64_t starting_version = creds->version_.load();

      //
      // Should trigger trivial copy
      //
      result = creds->creds_;

      std::uint64_t ending_version = creds->version_.load();
  } while (starting_version != ending_version);

  return result;
}

