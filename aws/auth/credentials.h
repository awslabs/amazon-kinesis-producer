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
#include <initializer_list>
#include <mutex>
#include <regex>
#include <string>

#include <boost/optional.hpp>

#include <aws/http/ec2_metadata.h>
#include <aws/utils/executor.h>
#include <aws/mutex.h>
#include <aws/utils/spin_lock.h>

namespace aws {
namespace auth {

class AwsCredentials {
 public:
  AwsCredentials(std::string akid,
                 std::string secret_key,
                 boost::optional<std::string> session_token = boost::none)
      : akid_(std::move(akid)),
        secret_key_(std::move(secret_key)),
        session_token_(std::move(session_token)) {}

  const std::string& akid() const noexcept {
    return akid_;
  }

  const std::string& access_key_id() const noexcept {
    return akid_;
  }

  const std::string& secret_key() const noexcept {
    return secret_key_;
  }

  const boost::optional<std::string>& session_token() const noexcept {
    return session_token_;
  }

 private:
  std::string akid_;
  std::string secret_key_;
  boost::optional<std::string> session_token_;
};

//------------------------------------------------------------------------------

class AwsCredentialsProvider : private boost::noncopyable {
 public:
  virtual AwsCredentials get_credentials();
  virtual boost::optional<AwsCredentials> try_get_credentials();

 protected:
  static const std::regex kAkidRegex;
  static const std::regex kSkRegex;

  static bool verify_credentials_format(const AwsCredentials& creds);

  virtual AwsCredentials get_credentials_impl() = 0;
};

//------------------------------------------------------------------------------

class EnvVarAwsCredentialsProvider : public AwsCredentialsProvider {
 protected:
  AwsCredentials get_credentials_impl() override;
};

//------------------------------------------------------------------------------

class BasicAwsCredentialsProvider : public AwsCredentialsProvider {
 public:
  BasicAwsCredentialsProvider(
      const std::string& akid,
      const std::string& secret_key,
      boost::optional<std::string> session_token = boost::none)
      : creds_(akid, secret_key, session_token) {}

 protected:
  AwsCredentials get_credentials_impl() override {
    return creds_;
  }

 private:
  AwsCredentials creds_;
};

//------------------------------------------------------------------------------

class InstanceProfileAwsCredentialsProvider : public AwsCredentialsProvider {
 public:
  InstanceProfileAwsCredentialsProvider(
      const std::shared_ptr<aws::utils::Executor>& executor,
      const std::shared_ptr<aws::http::Ec2Metadata>& ec2_metadata);

  void refresh();

 protected:
  AwsCredentials get_credentials_impl() override;

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

class AwsCredentialsProviderChain : public AwsCredentialsProvider {
 public:
  static std::shared_ptr<AwsCredentialsProviderChain> create(
      std::initializer_list<
          std::shared_ptr<AwsCredentialsProvider>> providers);

  AwsCredentialsProviderChain() = default;

  AwsCredentialsProviderChain(
      std::initializer_list<std::shared_ptr<AwsCredentialsProvider>> providers)
    : providers_(providers) {}

  void reset(
      std::initializer_list<
          std::shared_ptr<AwsCredentialsProvider>> new_providers);

 protected:
  AwsCredentials get_credentials_impl() override;

 private:
  using Mutex = aws::utils::TicketSpinLock;
  using Lock = aws::lock_guard<Mutex>;

  Mutex mutex_;
  std::vector<std::shared_ptr<AwsCredentialsProvider>> providers_;
};

//------------------------------------------------------------------------------

class DefaultAwsCredentialsProviderChain : public AwsCredentialsProviderChain {
 public:
  DefaultAwsCredentialsProviderChain(
      std::shared_ptr<aws::utils::Executor> executor,
      std::shared_ptr<aws::http::Ec2Metadata> ec2_metadata)
      : AwsCredentialsProviderChain({
            std::make_shared<EnvVarAwsCredentialsProvider>(),
            std::make_shared<InstanceProfileAwsCredentialsProvider>(
                std::move(executor),
                std::move(ec2_metadata))}) {}
};

} // namespace auth
} // namespace aws

#endif
