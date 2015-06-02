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

#ifndef AWS_UTILS_JSON_H_
#define AWS_UTILS_JSON_H_

#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/prettywriter.h>

namespace aws {
namespace utils {

// Wrapper around rapidjson for more fluent json processing
class Json {
 public:
  Json(const std::string& s)
      : doc_(std::make_unique<rapidjson::Document>()),
        v_(doc_.get()) {
    doc_->Parse(s.c_str());
    if (doc_->HasParseError()) {
      throw std::runtime_error("Could not parse json");
    }
  }

  Json(rapidjson::Value* v)
      : v_(v) {}

  // Unlike javascript, we allow the [] operator on nulls, which returns null.
  // This allows us to quickly check whether a nested item exists without
  // having to check the outer objects.
  Json operator [](const char* s) {
    if (v_ != nullptr) {
      if (!v_->IsObject()) {
        throw std::runtime_error("Cannot use [] on non-object value");
      }
      if (v_->HasMember(s)) {
        auto& v = (*v_)[s];
        if (!v_->IsNull()) {
          return Json(&v);
        }
      }
    }

    return Json(nullptr);
  }

  Json operator [](const std::string& s) {
    return (*this)[s.c_str()];
  }

  Json operator [](int i) {
    if (v_ == nullptr || !v_->IsArray()) {
      throw std::runtime_error("Cannot use [int] on non-arrays");
    }

    if (v_->Size() > (size_t) i) {
      auto it = v_->Begin();
      it += i;
      return Json(&*it);
    }

    return Json(nullptr);
  }

  size_t size() {
    if (v_ == nullptr || !v_->IsArray()) {
      throw std::runtime_error("Cannot call size on non-arrays");
    }
    return v_->Size();
  }

  operator std::string() {
    if (v_ == nullptr || !v_->IsString()) {
      throw std::runtime_error("Value is not a string");
    }
    return v_->GetString();
  }

  // We have an arbitrary definition of falseness that's different from
  // javascript in these ways:
  // - Empty arrays are false
  // - Number 0 is true
  // - Empty string is true
  operator bool() {
    if (v_ != nullptr) {
      if (v_->IsBool()) {
        return v_->GetBool();
      } else if (v_->IsString() || v_->IsNumber() || v_->IsObject()) {
        return true;
      } else if (v_->IsArray()) {
        return v_->Size() > 0;
      }
    }

    return false;
  }

 private:
  std::unique_ptr<rapidjson::Document> doc_;
  rapidjson::Value* v_;
};

} //namespace utils
} //namespace aws

#endif //AWS_UTILS_JSON_H_
