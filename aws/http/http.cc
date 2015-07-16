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

#include <sstream>

#include <boost/predef.h>

#include <aws/http/http.h>

#if BOOST_OS_WINDOWS
  #include <windows.h>
#else
  #include <sys/utsname.h>
#endif

namespace {

const constexpr char* kVersion = "0.10.0";

std::string user_agent() {
  std::stringstream ss;
  ss << "KinesisProducerLibrary/" << kVersion << " | ";
#if BOOST_OS_WINDOWS
  OSVERSIONINFO v;
  v.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);
  GetVersionEx(&v);
  ss << "Windows | " << v.dwMajorVersion << "." << v.dwMinorVersion << "."
     << v.dwBuildNumber << "." << v.dwPlatformId;
#else
  ::utsname un;
  ::uname(&un);
  ss << un.sysname << " | " << un.release << " | " << un.version << " | "
     << un.machine;
#endif
  return ss.str();
}

} //namespace

namespace aws {
namespace http {

HttpRequest create_kinesis_request(const std::string& region,
                                   const std::string& api_method,
                                   const std::string& data) {
  HttpRequest req(data.empty() ? "GET" : "POST", "/");
  req.add_header("Host", "kinesis." + region + ".amazonaws.com");
  req.add_header("Connection", "Keep-Alive");
  req.add_header("X-Amz-Target", "Kinesis_20131202." + api_method);
  req.add_header("User-Agent", user_agent());
  if (!data.empty()) {
    req.add_header("Content-Type", "application/x-amz-json-1.1");
  }
  req.set_data(data);
  return req;
}

} // namespace http
} // namepsace aws
