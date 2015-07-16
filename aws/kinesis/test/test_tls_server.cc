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

#include <aws/kinesis/test/test_tls_server.h>

#include <aws/utils/logging.h>

namespace aws {
namespace kinesis {
namespace test {

constexpr const int TestTLSServer::kDefaultPort;

TestTLSServer::TestTLSServer(int port)
    : port_(port),
      executor_(4),
      io_service_(executor_),
      endpoint_(boost::asio::ip::tcp::v4(), port_),
      acceptor_(executor_, endpoint_),
      ssl_ctx_(boost::asio::ssl::context::tlsv1_server) {
  ssl_ctx_.use_certificate(boost::asio::buffer(detail::kPem),
                           boost::asio::ssl::context::pem);
  ssl_ctx_.use_private_key(boost::asio::buffer(detail::kPem),
                           boost::asio::ssl::context::pem);
  accept_one();
}

void TestTLSServer::enqueue_handler(RequestHandler&& handler) {
  Lock lk(mutex_);
  handlers_.push_back(std::forward<RequestHandler>(handler));
}

void TestTLSServer::accept_one() {
  // Have to use a raw pointer here because libc++ has a bug with capturing
  // shared ptr by val with a lambda inside a lambda. The fix is to explicitly
  // init the captured variable with a move. However, that causes a segfault
  // with libstdc++.
  auto socket = new SslSocket(io_service_, ssl_ctx_);
  acceptor_.async_accept(
      socket->lowest_layer(),
      [=](const auto& ec) {
        if (ec) {
          LOG(warning) << "Test server error while accepting: "
                       << ec.message();
          delete socket;
          return;
        }

        socket->async_handshake(
            boost::asio::ssl::stream_base::server,
            [=](auto& ec) {
              this->read(ec, std::shared_ptr<SslSocket>(socket));
            });

        this->accept_one();
      });
}

void TestTLSServer::read(const boost::system::error_code& ec,
                         std::shared_ptr<SslSocket> socket) {
  if (ec) {
    LOG(warning) << "Test server error while handshaking: " << ec.message();
    return;
  }

  auto req = std::make_shared<aws::http::HttpRequest>();
  auto buf = std::make_shared<std::vector<char>>(64 * 1024);

  boost::asio::async_read(
      *socket,
      boost::asio::buffer(*buf),
      [=](auto& ec, auto n) -> size_t {
        if (ec) {
          return 0;
        }

        req->update(buf->data(), n);
        if (req->complete()) {
          return 0;
        } else {
          return buf->size();
        }
      },
      [=](auto& ec, auto num_read) {
        this->write(ec, req, socket);
      });
}

void TestTLSServer::write(const boost::system::error_code& ec,
                          std::shared_ptr<aws::http::HttpRequest> req,
                          std::shared_ptr<SslSocket> socket) {
  if (ec) {
    LOG(warning) << "Test server error while reading request: " << ec.message();
    socket->lowest_layer().close();
    return;
  }

  RequestHandler handler = detail::kEchoHandler;

  {
    Lock lk(mutex_);
    if (!handlers_.empty()) {
      handler = handlers_.front();
      handlers_.pop_front();
    }
  }

  auto response = new std::string(handler(*req).to_string());

  boost::asio::async_write(
      *socket,
      boost::asio::buffer(*response),
      [=](auto& ec, auto num_wrote) {
        delete response;

        if (ec) {
          LOG(warning) << "Test server error while writing response: "
                       << ec.message();
          socket->lowest_layer().close();
        } else {
          this->read(boost::system::error_code(), socket);
        }
      });
}

} //namespace test
} //namespace kinesis
} //namespace aws
