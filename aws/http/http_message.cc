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

#include <aws/http/http_message.h>

#include <sstream>

#include <boost/algorithm/string/case_conv.hpp>
#include <boost/algorithm/string/trim.hpp>

namespace {
  static std::string kContentLength = "Content-Length";
} // namespace

namespace aws {
namespace http {

HttpMessage::HttpMessage(bool no_parser, http_parser_type parse_type)
    : no_content_length_(false) {
  if (no_parser) {
    complete_ = true;
    return;
  } else {
    complete_ = false;
    parser_ = std::make_unique<http_parser>();
  }

  http_parser_init(parser_.get(), parse_type);
  parser_->data = this;

  static auto default_cb = [](auto parser) {
    return 0;
  };

  static auto default_data_cb = [](auto parser, auto data, auto len) {
    return 0;
  };

  settings_.on_message_begin = default_cb;
  settings_.on_url = default_data_cb;
  settings_.on_status = default_data_cb;
  settings_.on_header_field = default_data_cb;
  settings_.on_header_value = default_data_cb;
  settings_.on_headers_complete = default_cb;

  settings_.on_header_field = [](auto parser, auto data, auto len) {
    auto m = static_cast<HttpMessage*>(parser->data);
    auto header_name = std::string(data, len);
    m->add_header(header_name, "");
    return 0;
  };

  settings_.on_header_value = [](auto parser, auto data, auto len) {
    auto m = static_cast<HttpMessage*>(parser->data);
    auto header_val = std::string(data, len);
    m->headers_.back().second = header_val;
    return 0;
  };

  settings_.on_body = [](auto parser, auto data, auto len) {
    auto m = static_cast<HttpMessage*>(parser->data);
    m->data_.append(data, len);
    return 0;
  };

  settings_.on_message_complete = [](auto parser) {
    auto m = static_cast<HttpMessage*>(parser->data);
    m->complete_ = true;
    // Add a nul so we can use the data as a C string (needed by rapidjson)
    m->data_ += "\0";
    m->on_message_complete(parser);
    return 0;
  };
}

void HttpMessage::add_header(const std::string& name,
                             const std::string& value) {
  headers_.emplace_back(std::piecewise_construct,
                        std::forward_as_tuple(name),
                        std::forward_as_tuple(value));
}

size_t HttpMessage::remove_headers(std::string name) {
  boost::trim(name);
  boost::to_lower(name);

  size_t count = 0;
  for (auto it = headers_.begin(); it != headers_.end();) {
    auto n = it->first;
    boost::trim(n);
    boost::to_lower(n);
    if (n == name) {
      it = headers_.erase(it);
      count++;
    } else {
      it++;
    }
  }
  return count;
}

void HttpMessage::set_data(const std::string& s) {
  data_ = s;

  if (!no_content_length_) {
    for (auto& h : headers_) {
      if (h.first == kContentLength) {
        h.second = std::to_string(data_.length());
        return;
      }
    }

    headers_.emplace_back(
        std::piecewise_construct,
        std::forward_as_tuple(kContentLength),
        std::forward_as_tuple(std::to_string(data_.length())));
  }
}

void HttpMessage::update(char* data, size_t len) {
  size_t n = http_parser_execute(parser_.get(), &settings_, data, len);
  if (n != len) {
    std::stringstream ss;
    ss << "Error parsing http message, last input was: \n"
       << std::string(data, len) << "\n"
       << "Input was " << len << " bytes, only consumed " << n;
    throw std::runtime_error(ss.str().c_str());
  }
}

} //namespace http
} //namespace aws
