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

#ifndef AWS_KINESIS_TEST2_TEST_TLS_SERVER_H_
#define AWS_KINESIS_TEST2_TEST_TLS_SERVER_H_

#include <list>

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>

#include <aws/http/http_request.h>
#include <aws/http/http_response.h>
#include <aws/utils/io_service_executor.h>

namespace aws {
namespace kinesis {
namespace test {

namespace detail {

// Self signed cert
static const std::string kPem =
R"(-----BEGIN CERTIFICATE-----
MIICmTCCAgICCQCBo19wkLcccDANBgkqhkiG9w0BAQUFADCBkDELMAkGA1UEBhMC
VVMxEzARBgNVBAgTCkNhbGlmb3JuaWExEjAQBgNVBAcTCVBhbG8gQWx0bzEPMA0G
A1UEChMGQW1hem9uMRAwDgYDVQQLEwdLaW5lc2lzMREwDwYDVQQDEwhDaGFvZGVu
ZzEiMCAGCSqGSIb3DQEJARYTY2hhb2RlbmdAYW1hem9uLmNvbTAeFw0xNTAzMzAw
NjE5MjdaFw0yNTAzMjcwNjE5MjdaMIGQMQswCQYDVQQGEwJVUzETMBEGA1UECBMK
Q2FsaWZvcm5pYTESMBAGA1UEBxMJUGFsbyBBbHRvMQ8wDQYDVQQKEwZBbWF6b24x
EDAOBgNVBAsTB0tpbmVzaXMxETAPBgNVBAMTCENoYW9kZW5nMSIwIAYJKoZIhvcN
AQkBFhNjaGFvZGVuZ0BhbWF6b24uY29tMIGfMA0GCSqGSIb3DQEBAQUAA4GNADCB
iQKBgQDPu9sNK/+dXprQ9uS76EoEK7/HiYbNRQNRgFgOulXO8gxsPnGLDTMNCaI5
L2Iy2HkiNsiwCRaIB+5oXeg5DD1qNunit2GjLnzpm0orFpFd4kosEjfDsaVmc9uj
+wgvp5OhHgkv//sGRQLiiFEtILaCtaDQx4hGoCvP1FFRiag1PQIDAQABMA0GCSqG
SIb3DQEBBQUAA4GBAExSTJhmHmVXa5v9dc0jnaHQuLVZKPgf31f2Ndh3eLl6oxgd
8TtKwcky9cjXi50Z4UsIF4Vnrg/1HDiZRTl+Yq6DD77MH9Wf6Dlu69M8qk86zaMY
5nBvEDQ3NQJyqEUCnRXd2GS2ajG18E4Iv1yF4Z+IQxNQA79tQThtyDbaD2AS
-----END CERTIFICATE-----
-----BEGIN RSA PRIVATE KEY-----
MIICXQIBAAKBgQDPu9sNK/+dXprQ9uS76EoEK7/HiYbNRQNRgFgOulXO8gxsPnGL
DTMNCaI5L2Iy2HkiNsiwCRaIB+5oXeg5DD1qNunit2GjLnzpm0orFpFd4kosEjfD
saVmc9uj+wgvp5OhHgkv//sGRQLiiFEtILaCtaDQx4hGoCvP1FFRiag1PQIDAQAB
AoGBAK3YqsNlNPBAQhPq6xWOmpLPAho9L8ENpm9Il2kL68/apSbZQzB5hWW90DNH
QkkG/KjzbBRWJrME4DIblMJZSfLdsiQZtbxbAU9vx1wKDtxsVLMpTRAAN00pw/3/
sd20Ced056yd8VkVv0Z7QncV2vi86eS7r5YhvJJEu39v+LZBAkEA5t1/vuR5AyqU
c1Q9hoakGEf/I9vC9IBkwyjTyWCfaMFTPOV+XFmp1IfbN5y5f0BF5LDbfMc1/ht6
Qm76oj9y+QJBAOZZp752PzxB3PzphbYBCvjWjeovfIcA5Dknza8q1picDNsVvW5t
02lpgVoqmMPHJKqc/2UKaagvNxWRZvjr4WUCQQCB1cX3HFS2JCcyqQik9GmqwirK
BtigWujQHNDmqvFbn4XpdINY+pAZV4JAx2JHH2VvVMtLZFmIG/npDLLltls5AkBt
QSDaqWMcxXB3VJti0+PMWpc8+ADsV3Pn2AUqi/r0ry85ynnqopSfbrc3ePS0BwRR
F93aorGyX5KU3D4m2loxAkBvMo1xJ5GlljhP0tJidXfL3IB0XRWbii+HXq2o9Q3S
aPfUBCyWL5ZFCbzAHDVkwjGvbOCFL+Px13UFdOGe5pry
-----END RSA PRIVATE KEY-----)";

static const auto kEchoHandler = [](const auto& request) {
  aws::http::HttpResponse response(200);
  for (const auto& p : request.headers()) {
    response.add_header(p.first, p.second);
  }
  response.set_data(request.data());
  return response;
};


} //namespace detail

class TestTLSServer : boost::noncopyable {
 public:
  static constexpr const int kDefaultPort = 45278;

  using RequestHandler =
    std::function<aws::http::HttpResponse (const aws::http::HttpRequest&)>;

  TestTLSServer(int port = kDefaultPort);

  void enqueue_handler(RequestHandler&& handler);

 private:
  using SslSocket = boost::asio::ssl::stream<boost::asio::ip::tcp::socket>;
  using Mutex = aws::mutex;
  using Lock = aws::lock_guard<Mutex>;

  void accept_one();

  void read(const boost::system::error_code& ec,
            std::shared_ptr<SslSocket> socket);

  void write(const boost::system::error_code& ec,
             std::shared_ptr<aws::http::HttpRequest> req,
             std::shared_ptr<SslSocket> socket);

  const int port_;
  std::list<RequestHandler> handlers_;
  aws::utils::IoServiceExecutor executor_;
  boost::asio::io_service& io_service_;
  boost::asio::ip::tcp::endpoint endpoint_;
  boost::asio::ip::tcp::acceptor acceptor_;
  boost::asio::ssl::context ssl_ctx_;
  Mutex mutex_;
};

} //namespace test
} //namespace kinesis
} //namespace aws

#endif //AWS_KINESIS_TEST2_TEST_TLS_SERVER_H_
