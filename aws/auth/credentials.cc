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

AwsCredentials AwsCredentialsProvider::get_credentials() {
  AwsCredentials creds = get_credentials_impl();
  if (!verify_credentials_format(creds)) {
    throw std::runtime_error("Credentials did not match expected patterns");
  }
  return creds;
}

boost::optional<AwsCredentials> AwsCredentialsProvider::try_get_credentials() {
  try {
    return get_credentials();
  } catch (const std::exception&) {
    return boost::none;
  }
}

const std::regex AwsCredentialsProvider::kAkidRegex(
    "^[A-Z0-9]{20}$",
    std::regex::ECMAScript | std::regex::optimize);

const std::regex AwsCredentialsProvider::kSkRegex(
    "^[A-Za-z0-9/+=]{40}$",
    std::regex::ECMAScript | std::regex::optimize);

// static
bool AwsCredentialsProvider::verify_credentials_format(
    const AwsCredentials& creds) {
  return std::regex_match(creds.akid(), kAkidRegex) &&
         std::regex_match(creds.secret_key(), kSkRegex);
}

//------------------------------------------------------------------------------

AwsCredentials EnvVarAwsCredentialsProvider::get_credentials_impl() {
  char* akid = std::getenv("AWS_ACCESS_KEY_ID");
  char* sk = std::getenv("AWS_SECRET_KEY");
  if (sk == nullptr) {
    sk = std::getenv("AWS_SECRET_ACCESS_KEY");
  }
  if (akid == nullptr || sk == nullptr) {
    throw std::runtime_error("Could not get credentials from env vars");
  }
  return AwsCredentials(akid, sk);
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

AwsCredentials InstanceProfileAwsCredentialsProvider::get_credentials_impl() {
  Lock lk(mutex_);
  if (akid_.empty()) {
    throw std::runtime_error(
        "Instance profile credentials not loaded from ec2 metadata yet");
  }
  return AwsCredentials(akid_, secret_key_, session_token_);
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
      LOG(error) << "Could not fetch instance profile credentials after "
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

    LOG(info) << "Got credentials from instance profile. Refreshing in "
              << std::chrono::duration_cast<std::chrono::seconds>(
                    refresh_at - std::chrono::steady_clock::now()).count()
                        / 3600.0
              << " hours";

    attempt_ = 0;
  } catch (const std::exception& ex) {
    LOG(error) << "Error parsing instance profile credentials json: "
               << ex.what();
    retry();
  }
}

//------------------------------------------------------------------------------

// static
std::shared_ptr<AwsCredentialsProviderChain>
AwsCredentialsProviderChain::create(
    std::initializer_list<
        std::shared_ptr<AwsCredentialsProvider>> providers) {
  auto c = std::make_shared<AwsCredentialsProviderChain>();
  c->reset(providers);
  return c;
}

void AwsCredentialsProviderChain::reset(
    std::initializer_list<
        std::shared_ptr<AwsCredentialsProvider>> new_providers) {
  Lock lk(mutex_);

  providers_.clear();
  providers_.insert(providers_.cend(), std::move(new_providers));
}

AwsCredentials AwsCredentialsProviderChain::get_credentials_impl() {
  Lock lk(mutex_);

  if (providers_.empty()) {
    throw std::runtime_error("The AwsCredentialsProviderChain is empty");
  }

  for (auto p : providers_) {
    auto creds = p->try_get_credentials();
    if (creds) {
      return *creds;
    }
  }

  throw std::runtime_error("Could not get credentials from any of the "
                           "providers in the AwsCredentialsProviderChain");
}

} // namespace auth
} // namespace aws
