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

using namespace aws::auth;

MutableStaticCredentialsProvider::MutableStaticCredentialsProvider(const std::string& akid,
                                                                   const std::string& sk,
                                                                   std::string token) {
  creds_ = std::make_shared<Aws::Auth::AWSCredentials>(akid, sk, token);
}

void MutableStaticCredentialsProvider::set_credentials(const std::string& akid, const std::string& sk, std::string token) {
  std::lock_guard<std::mutex> lock(update_mutex_);

  std::shared_ptr<Aws::Auth::AWSCredentials> new_credentials = std::make_shared<Aws::Auth::AWSCredentials>(akid, sk, token);
  Aws::Auth::AWSCredentials new_creds(akid, sk, token);
  std::atomic_store(&creds_, new_credentials);
//  *creds_ = new_creds;
//  std::atomic_store(&creds_, new_credentials);
}

Aws::Auth::AWSCredentials MutableStaticCredentialsProvider::GetAWSCredentials() {
  std::shared_ptr<Aws::Auth::AWSCredentials> result = std::atomic_load(&creds_);
  return *result;
}
