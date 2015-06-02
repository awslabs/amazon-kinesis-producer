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

#include <regex>

#include <boost/algorithm/string/trim.hpp>
#include <boost/algorithm/string/split.hpp>

#include <aws/auth/credentials.h>
#include <aws/utils/utils.h>
#include <aws/http/ec2_metadata.h>
#include <aws/utils/json.h>

namespace aws {
namespace auth {

boost::optional<AwsCredentials> AwsCredentialsProvider::get_credentials() {
  auto akid = get_akid();
  auto sk = get_secret_key();
  if (akid && sk) {
    AwsCredentials c;
    c.akid = akid.get();
    c.secret_key = sk.get();
    c.session_token = get_session_token();
    if (verify_credentials_format(c)) {
      return c;
    }
  }
  return boost::none;
}

const std::regex AwsCredentialsProvider::kAkidRegex(
    "^[A-Z0-9]{20}$",
    std::regex::ECMAScript | std::regex::optimize);

const std::regex AwsCredentialsProvider::kSkRegex(
    "^[A-Za-z0-9/+=]{40}$",
    std::regex::ECMAScript | std::regex::optimize);

bool AwsCredentialsProvider::verify_credentials_format(
    const AwsCredentials& creds) {
  return std::regex_match(creds.akid, kAkidRegex) &&
         std::regex_match(creds.secret_key, kSkRegex);
}

//------------------------------------------------------------------------------

boost::optional<std::string> EnvVarAwsCredentialsProvider::get_akid() {
  char* akid = std::getenv("AWS_ACCESS_KEY_ID");
  return akid ? boost::optional<std::string>(akid) : boost::none;
}

boost::optional<std::string> EnvVarAwsCredentialsProvider::get_secret_key() {
  char* sk = std::getenv("AWS_SECRET_KEY");
  if (!sk) {
    sk = std::getenv("AWS_SECRET_ACCESS_KEY");
  }
  return sk ? boost::optional<std::string>(sk) : boost::none;
}

//------------------------------------------------------------------------------

BasicAwsCredentialsProvider::BasicAwsCredentialsProvider(
    const std::string& akid,
    const std::string& secret_key)
    : akid_(akid),
      secret_key_(secret_key) {}

boost::optional<std::string> BasicAwsCredentialsProvider::get_akid() {
  return akid_;
}

boost::optional<std::string> BasicAwsCredentialsProvider::get_secret_key() {
  return secret_key_;
}

//------------------------------------------------------------------------------

InstanceProfileAwsCredentialsProvider::InstanceProfileAwsCredentialsProvider(
    const std::shared_ptr<aws::utils::Executor>& executor,
    const std::shared_ptr<aws::http::Ec2Metadata>& ec2_metadata)
    : executor_(executor),
      ec2_metadata_(ec2_metadata),
      attempt_(0) {
  refresh();
}

boost::optional<std::string>
InstanceProfileAwsCredentialsProvider::get_akid() {
  Lock lk(mutex_);
  if (!akid_.empty()) {
    return akid_;
  } else {
    return boost::none;
  }
}

boost::optional<std::string>
InstanceProfileAwsCredentialsProvider::get_secret_key() {
  Lock lk(mutex_);
  if (!secret_key_.empty()) {
    return secret_key_;
  } else {
    return boost::none;
  }
}

boost::optional<std::string>
InstanceProfileAwsCredentialsProvider::get_session_token() {
  Lock lk(mutex_);
  if (!session_token_.empty()) {
    return session_token_;
  } else {
    return boost::none;
  }
}

void InstanceProfileAwsCredentialsProvider::refresh() {
  ec2_metadata_->get_instance_profile_credentials(
      [this](bool success, auto& result) {
        this->handle_result(success, result);
      });
}

void InstanceProfileAwsCredentialsProvider::handle_result(
    bool success,
    const std::string& result) {
  Lock lk(mutex_);

  auto retry = [this] {
    attempt_++;
    if (attempt_ >= 10) {
      LOG(ERROR) << "Could not fetch instance profile credentials after "
                 << attempt_ << " attempts.";
      return;
    }
    executor_->schedule([this] { this->refresh(); },
                        std::chrono::milliseconds(attempt_ * 100));
  };

  if (!success) {
    retry();
    return;
  }

  try {
    auto json = aws::utils::Json(result);
    akid_ = (std::string) json["AccessKeyId"];
    secret_key_ = (std::string) json["SecretAccessKey"];
    auto token = json["Token"];
    if (token) {
      session_token_ = (std::string) token;
    }

    auto expires_at = aws::utils::steady_tp_from_str(json["Expiration"]);
    auto refresh_at = expires_at - std::chrono::minutes(3);
    executor_->schedule([this] { this->refresh(); }, refresh_at);

    LOG(INFO) << "Got credentials from instance profile. Refreshing in "
              << std::chrono::duration_cast<std::chrono::seconds>(
                    refresh_at - std::chrono::steady_clock::now()).count()
                        / 3600.0
              << " hours";

    attempt_ = 0;
  } catch (const std::exception& ex) {
    LOG(ERROR) << "Error parsing instance profile credentials json: "
               << ex.what();
    retry();
  }
}

//------------------------------------------------------------------------------

DefaultAwsCredentialsProvider::DefaultAwsCredentialsProvider(
    const std::shared_ptr<aws::utils::Executor>& executor,
    const std::shared_ptr<aws::http::Ec2Metadata>& ec2_metadata)
    : profile_provider_(
          std::make_unique<InstanceProfileAwsCredentialsProvider>(
              executor,
              ec2_metadata)) {}

boost::optional<AwsCredentials>
DefaultAwsCredentialsProvider::get_credentials() {
  auto from_env_var = env_var_provider_.get_credentials();
  if (from_env_var) {
    return from_env_var;
  } else {
    return profile_provider_->get_credentials();
  }
}

void DefaultAwsCredentialsProvider::refresh() {
  env_var_provider_.refresh();
  profile_provider_->refresh();
}

} // namespace auth
} // namespace aws
