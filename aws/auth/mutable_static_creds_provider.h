// Copyright 2015 Amazon.com, Inc. or its affiliates. All Rights Reserved.
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
#include <aws/utils/spin_lock.h>

namespace aws {
namespace auth {

// Like basic static creds, but with an atomic set operation
class MutableStaticCredentialsProvider
    : public Aws::Auth::AWSCredentialsProvider {
 public:
  MutableStaticCredentialsProvider(std::string akid,
                                   std::string sk,
                                   std::string token = "")
      : creds_(akid, sk, token) {}

  void set_credentials(std::string akid,
                       std::string sk,
                       std::string token = "") {
    Lock lock_(mutex_);
    creds_.SetAWSAccessKeyId(akid);
    creds_.SetAWSSecretKey(sk);
    creds_.SetSessionToken(token);
  }

  Aws::Auth::AWSCredentials GetAWSCredentials() override {
    Lock lock_(mutex_);
    return creds_;
  }

 private:
  using Mutex = aws::utils::TicketSpinLock;
  using Lock = std::lock_guard<Mutex>;
  Mutex mutex_;
  Aws::Auth::AWSCredentials creds_;
};

} //namespace auth
} //namespace aws

#endif //AWS_AUTH_MUTABLE_STATIC_CREDS_PROVIDER_H_
