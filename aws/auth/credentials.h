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

#ifndef AWS_AUTH_CREDENTIALS_H_
#define AWS_AUTH_CREDENTIALS_H_

#include <chrono>
#include <mutex>
#include <regex>
#include <string>

#include <boost/optional.hpp>

#include <aws/http/ec2_metadata.h>
#include <aws/utils/executor.h>
#include <aws/mutex.h>

namespace aws {
namespace auth {

struct AwsCredentials {
  std::string akid;
  std::string secret_key;
  boost::optional<std::string> session_token;
};

//------------------------------------------------------------------------------

class AwsCredentialsProvider : private boost::noncopyable {
 public:
  virtual boost::optional<AwsCredentials> get_credentials();

  virtual void refresh() {}

 protected:
  virtual boost::optional<std::string> get_akid() {
    return boost::none;
  }

  virtual boost::optional<std::string> get_secret_key() {
    return boost::none;
  }

  virtual boost::optional<std::string> get_session_token() {
    return boost::none;
  }

 private:
  static const std::regex kAkidRegex;
  static const std::regex kSkRegex;

  static bool verify_credentials_format(const AwsCredentials& creds);
};

//------------------------------------------------------------------------------

class EnvVarAwsCredentialsProvider : public AwsCredentialsProvider {
 protected:
  boost::optional<std::string> get_akid() override;
  boost::optional<std::string> get_secret_key() override;
  boost::optional<std::string> get_session_token() override;
};

//------------------------------------------------------------------------------

class BasicAwsCredentialsProvider : public AwsCredentialsProvider {
 public:
  explicit BasicAwsCredentialsProvider(const std::string& akid,
                                       const std::string& secret_key);

 protected:
  boost::optional<std::string> get_akid() override;
  boost::optional<std::string> get_secret_key() override;

 private:
  std::string akid_;
  std::string secret_key_;
};

//------------------------------------------------------------------------------

class InstanceProfileAwsCredentialsProvider : public AwsCredentialsProvider {
 public:
  InstanceProfileAwsCredentialsProvider(
      const std::shared_ptr<aws::utils::Executor>& executor,
      const std::shared_ptr<aws::http::Ec2Metadata>& ec2_metadata);

  void refresh() override;

 protected:
  boost::optional<std::string> get_akid() override;
  boost::optional<std::string> get_secret_key() override;
  boost::optional<std::string> get_session_token() override;

 private:
  using Mutex = aws::mutex;
  using Lock = aws::unique_lock<Mutex>;

  void handle_result(bool success, const std::string& result);

  std::shared_ptr<aws::utils::Executor> executor_;
  std::shared_ptr<aws::http::Ec2Metadata> ec2_metadata_;
  std::string akid_;
  std::string secret_key_;
  std::string session_token_;
  Mutex mutex_;
  int attempt_;
};

//------------------------------------------------------------------------------

class DefaultAwsCredentialsProvider : public AwsCredentialsProvider {
 public:
  explicit DefaultAwsCredentialsProvider(
      const std::shared_ptr<aws::utils::Executor>& executor,
      const std::shared_ptr<aws::http::Ec2Metadata>& ec2_metadata);

  boost::optional<AwsCredentials> get_credentials() override;

  void refresh() override;

 private:
  std::unique_ptr<InstanceProfileAwsCredentialsProvider> profile_provider_;
  EnvVarAwsCredentialsProvider env_var_provider_;
};

} // namespace auth
} // namespace aws

#endif
