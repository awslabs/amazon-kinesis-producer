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

// Like basic static creds, but with an atomic set operation
class MutableStaticCredentialsProvider
    : public Aws::Auth::AWSCredentialsProvider {
 public:
  MutableStaticCredentialsProvider(const std::string& akid, const std::string& sk, std::string token = "");

  void set_credentials(const std::string& akid, const std::string& sk, std::string token = "");

  Aws::Auth::AWSCredentials GetAWSCredentials() override;

 private:
  std::mutex update_mutex_;
  std::shared_ptr<Aws::Auth::AWSCredentials> creds_;

};

} //namespace auth
} //namespace aws

#endif //AWS_AUTH_MUTABLE_STATIC_CREDS_PROVIDER_H_
