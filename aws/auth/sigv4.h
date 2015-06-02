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

#ifndef AWS_AUTH_SIGV4_H_
#define AWS_AUTH_SIGV4_H_

#include <aws/http/http.h>
#include <aws/auth/credentials.h>
#include <aws/utils/utils.h>

namespace aws {
namespace auth {

class RequestSigner;

class SigV4Context {
 public:
  SigV4Context(
      const std::string& region,
      const std::string& service,
      const std::shared_ptr<AwsCredentialsProvider>& credentials_provider);

  const std::shared_ptr<AwsCredentialsProvider>& credentials_provider() {
    return credentials_provider_;
  }

 private:
  friend class RequestSigner;

  std::string region_;
  std::string service_;
  std::shared_ptr<AwsCredentialsProvider> credentials_provider_;
};

class RequestSigner {
 public:
  RequestSigner(aws::http::HttpRequest& req,
                SigV4Context& ctx,
                bool x_amz_date = true,
                std::pair<std::string, std::string> date_time =
                    aws::utils::get_date_time());

  void sign();

  const std::string& canon_request() {
    return canon_request_;
  }

  const std::string& str_to_sign() {
    return str_to_sign_;
  }

  const std::string& auth_header() {
    return auth_header_;
  }

 private:
  void calculate_canon_query_args();
  void calculate_headers();
  void calculate_canon_headers();
  void calculate_credential_scope();
  void calculate_signed_headers();
  void calculate_canon_request();
  void calculate_str_to_sign();
  void calculate_auth_header();

  aws::http::HttpRequest& req_;
  SigV4Context& sig_v4_ctx_;

  std::vector<std::pair<std::string, std::string>> headers_;
  std::string date_;
  std::string time_;
  std::string date_time_;
  std::string canon_headers_;
  std::string canon_path_;
  std::string canon_query_args_;
  std::string credential_scope_;
  std::string signed_headers_;
  std::string canon_request_;
  std::string str_to_sign_;
  std::string auth_header_;

  std::string akid_;
  std::string secret_key_;
};

void sign_v4(aws::http::HttpRequest& req, SigV4Context& ctx);

} //namesppace auth
} //namespace aws

#endif //AWS_AUTH_SIGV4_H_
