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
  next_creds->updating_ = true;
  next_creds->creds_.SetAWSAccessKeyId(akid);
  next_creds->creds_.SetAWSSecretKey(sk);
  next_creds->creds_.SetSessionToken(token);
  next_creds->version_++;
  next_creds->updating_ = false;

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
      if (creds->updating_) {
        //
        // The credentials are currently being updated.  It's not safe to read so
        // spin while we wait for them to clear
        //
        continue;
      }
      std::uint64_t starting_version = creds->version_.load();

      //
      // Should trigger trivial copy
      //
      result = creds->creds_;

      if (creds->updating_) {
        //
        // The credentials object started to be updated possibly while we were
        // copying it, so discard what we have and try again.
        //
        continue;
      }
      
      std::uint64_t ending_version = creds->version_.load();

      if (starting_version != ending_version) {
        //
        // The version changed in between the start of the copy, and the end
        // of the copy.  We can no longer trust that the resulting copy is
        // correct.  So we give up and try again.
        //
        continue;
      }
      return result;
  } while (true);

  return result;
}

