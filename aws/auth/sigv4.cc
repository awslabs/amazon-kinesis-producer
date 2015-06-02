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

#include <aws/auth/sigv4.h>

#include <algorithm>
#include <iostream>
#include <sstream>

#include <boost/algorithm/string/case_conv.hpp>
#include <boost/algorithm/string/trim.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/replace.hpp>
#include <boost/algorithm/string/find.hpp>

#include <glog/logging.h>

#include <openssl/hmac.h>
#include <openssl/sha.h>

namespace {

static const char* ALGO = "AWS4-HMAC-SHA256";

template <typename Key, typename Data, typename Output>
void hmac_sha256(const Key& key, const Data& data, Output& output) {
  auto output_len = (unsigned int) output.size();
  auto ret = ::HMAC(EVP_sha256(),
                    key.data(),
                    key.size(),
                    (unsigned char*) data.data(),
                    data.size(),
                    output.data(),
                    &output_len);
  if (ret == nullptr) {
    throw std::runtime_error("Error computing HMAC-SHA256");
  }
}

template <typename Input>
std::string sha256_hex(const Input& input) {
  std::array<uint8_t, SHA256_DIGEST_LENGTH> digest;
  auto ret = ::SHA256((const unsigned char*) input.data(),
                      input.size(),
                      digest.data());
  if (ret == nullptr) {
    throw std::runtime_error("Error computing SHA256");
  }
  return aws::utils::hex(digest.data(), digest.size());
}

} // namespace

namespace aws {
namespace auth {

SigV4Context::SigV4Context(
    const std::string& region,
    const std::string& service,
    const std::shared_ptr<AwsCredentialsProvider>& credentials_provider)
    : region_(region),
      service_(service),
      credentials_provider_(credentials_provider) {}

RequestSigner::RequestSigner(aws::http::HttpRequest& req,
                             aws::auth::SigV4Context& ctx,
                             bool x_amz_date,
                             std::pair<std::string, std::string> date_time)
    : req_(req),
      sig_v4_ctx_(ctx) {
  date_ = std::move(date_time.first);
  time_ = std::move(date_time.second);
  date_time_ = date_ + "T" + time_ + "Z";
  if (x_amz_date) {
    req_.add_header("x-amz-date", date_time_);
  }
}

void RequestSigner::calculate_canon_query_args() {
  std::vector<std::string> split_url = utils::split_on_first(req_.path(), "?");

  std::vector<std::pair<std::string, std::string>> args;
  if (split_url.size() >= 2) {
    std::vector<std::string> v;
    boost::split(v, split_url[1], boost::is_any_of("&"));
    for (const auto& s : v) {
      std::vector<std::string> p;
      boost::split(p, s, boost::is_any_of("="));
      for (auto& a : p) {
        boost::trim(a);
      }
      if (p.size() == 2) {
        args.emplace_back(
            std::piecewise_construct,
            std::forward_as_tuple(p.at(0)),
            std::forward_as_tuple(p.at(1)));
      }
    }
    std::sort(args.begin(), args.end());
  }

  std::stringstream ss;
  for (auto it = args.cbegin(); it != args.cend(); ++it) {
    ss << it->first << "=" << it->second;
    if ((it + 1) != args.cend()) {
      ss << "&";
    }
  }
  canon_query_args_ = ss.str();

  canon_path_ = split_url.at(0);
}

void RequestSigner::calculate_headers() {
  for (const auto& h : req_.headers()) {
    headers_.push_back(h);
  }

  for (auto& p : headers_) {
    boost::to_lower(p.first);
    boost::trim(p.first);
    boost::trim(p.second);
  }

  std::sort(headers_.begin(), headers_.end());

  for (size_t i = 0; i < headers_.size(); ++i) {
    for (size_t j = i + 1; j < headers_.size(); ++j) {
      if (headers_[i].first == headers_[j].first) {
        headers_[i].second += ",";
        headers_[i].second += headers_[j].second;
        headers_.erase(headers_.begin() + j);
        --j;
      }
    }
  }
}

void RequestSigner::calculate_canon_headers() {
  std::stringstream ss;
  for (const auto& h : headers_) {
    ss << h.first << ":" << h.second << "\n";
  }
  canon_headers_ = ss.str();
}

void RequestSigner::calculate_credential_scope() {
  std::stringstream ss;
  ss << date_ << "/"
     << sig_v4_ctx_.region_ << "/"
     << sig_v4_ctx_.service_ << "/"
     << "aws4_request";
  credential_scope_ = ss.str();
}

void RequestSigner::calculate_signed_headers() {
  std::stringstream ss;
  for (auto it = headers_.begin(); it != headers_.end(); ++it) {
    ss << it->first;
    if ((it + 1) != headers_.end()) {
      ss << ";";
    }
  }
  signed_headers_ = ss.str();
}

void RequestSigner::calculate_canon_request() {
  std::stringstream ss;
  ss << req_.method() << "\n"
     << canon_path_ << "\n"
     << canon_query_args_ << "\n"
     << canon_headers_ << "\n"
     << signed_headers_ << "\n"
     << sha256_hex(req_.data());
  canon_request_ = ss.str();
}

void RequestSigner::calculate_str_to_sign() {
  std::stringstream ss;
  ss << ALGO << "\n"
     << date_time_ << "\n"
     << credential_scope_ << "\n"
     << sha256_hex(canon_request_);
  str_to_sign_ = ss.str();
}

void RequestSigner::calculate_auth_header() {
  std::array<uint8_t, SHA256_DIGEST_LENGTH> buff_1;
  std::array<uint8_t, SHA256_DIGEST_LENGTH> buff_2;
  hmac_sha256("AWS4" + secret_key_, date_, buff_1);
  hmac_sha256(buff_1, sig_v4_ctx_.region_, buff_2);
  hmac_sha256(buff_2, sig_v4_ctx_.service_, buff_1);
  hmac_sha256(buff_1, std::string("aws4_request"), buff_2);
  hmac_sha256(buff_2, str_to_sign_, buff_1);

  std::string signature = aws::utils::hex(buff_1.data(), buff_1.size());

  std::stringstream ss;
  ss << ALGO << " "
     << "Credential=" << akid_ << "/" << credential_scope_ << ", "
     << "SignedHeaders=" << signed_headers_ << ", "
     << "Signature=" << signature;
  auth_header_ = ss.str();
}

void RequestSigner::sign() {
  auto creds = sig_v4_ctx_.credentials_provider_->get_credentials();
  if (!creds) {
    throw std::runtime_error("Invalid credentials");
  }

  akid_ = creds->akid;
  secret_key_ = creds->secret_key;

  if (creds->session_token) {
    req_.add_header("x-amz-security-token", creds->session_token.get());
  }

  req_.remove_headers("authorization");

  calculate_canon_query_args();
  calculate_headers();
  calculate_canon_headers();
  calculate_signed_headers();
  calculate_credential_scope();
  calculate_canon_request();
  calculate_str_to_sign();
  calculate_auth_header();

  req_.add_header("Authorization", auth_header_);
}

void sign_v4(aws::http::HttpRequest& req, SigV4Context& ctx) {
  RequestSigner sr(req, ctx);
  sr.sign();
}

} // namespace auth
} // namespace aws
