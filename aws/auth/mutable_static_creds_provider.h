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

#ifndef AWS_AUTH_MUTABLE_STATIC_CREDS_PROVIDER_H_
#define AWS_AUTH_MUTABLE_STATIC_CREDS_PROVIDER_H_

#include <aws/core/auth/AWSCredentialsProvider.h>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <array>
#include <condition_variable>
#include <memory>

namespace aws {
namespace auth {

struct DebugStats {
  std::uint64_t update_before_load_;
  std::uint64_t update_after_load_;
  std::uint64_t version_mismatch_;
  std::uint64_t success_;
  std::uint64_t retried_;
  std::uint64_t attempts_;
  std::uint64_t used_lock_;

  DebugStats() : update_before_load_(0), update_after_load_(0), version_mismatch_(0),
                 success_(0), retried_(0), attempts_(0), used_lock_(0) {}
};

// Like basic static creds, but with an atomic set operation
class MutableStaticCredentialsProvider
    : public Aws::Auth::AWSCredentialsProvider {
 public:
  MutableStaticCredentialsProvider(const std::string& akid, const std::string& sk, std::string token = "");

  void set_credentials(const std::string& akid, const std::string& sk, std::string token = "");

  Aws::Auth::AWSCredentials GetAWSCredentials() override;

  DebugStats get_debug_stats();


 private:
  std::mutex update_mutex_;
  std::shared_ptr<Aws::Auth::AWSCredentials> creds_;

};

} //namespace auth
} //namespace aws

#endif //AWS_AUTH_MUTABLE_STATIC_CREDS_PROVIDER_H_
