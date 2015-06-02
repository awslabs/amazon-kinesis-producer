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

#ifndef AWS_HTTP_IO_SERVICE_SOCKET_H_
#define AWS_HTTP_IO_SERVICE_SOCKET_H_

#include <boost/asio.hpp>
#include <boost/asio/spawn.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/asio/steady_timer.hpp>

#include <glog/logging.h>

#include <aws/auth/certs.h>
#include <aws/http/socket.h>

namespace aws {
namespace http {

class IoServiceSocket : public Socket {
 public:
  IoServiceSocket(const std::shared_ptr<boost::asio::io_service>& io_service,
                  const std::string& endpoint,
                  int port,
                  bool secure,
                  bool verify_cert)
      : io_service_(io_service),
        timer_(*io_service_),
        ssl_ctx_(boost::asio::ssl::context::tlsv1_client),
        endpoint_(endpoint),
        port_(port),
        secure_(secure),
        verify_cert_(verify_cert) {}

  void open(const ConnectCallback& cb,
            std::chrono::milliseconds timeout) override {
    if (secure_) {
      ssl_ctx_.add_certificate_authority(
          boost::asio::buffer(
              std::string(aws::auth::VeriSignClass3PublicPrimaryG5)));
      ssl_ctx_.add_certificate_authority(
          boost::asio::buffer(
              std::string(aws::auth::VeriSignClass3PublicPrimary)));
      ssl_socket_ =
          std::make_shared<boost::asio::ssl::stream<TcpSocket>>(
              *io_service_,
              ssl_ctx_);
      if (verify_cert_) {
        ssl_socket_->set_verify_mode(boost::asio::ssl::verify_peer);
        ssl_socket_->set_verify_callback(
            boost::asio::ssl::rfc2818_verification(endpoint_));
      }
    } else {
      tcp_socket_ = std::make_shared<TcpSocket>(*io_service_);
    }

    set_timeout(timeout);

    boost::asio::spawn(*io_service_, [=](auto& yield) {
      this->open_coro(cb, yield);
    });
  }

  void write(const char* data,
             size_t len,
             const WriteCallback& cb,
             std::chrono::milliseconds timeout) override {
    if (ssl_socket_) {
      write_impl(ssl_socket_, data, len, cb, timeout);
    } else if (tcp_socket_) {
      write_impl(tcp_socket_, data, len, cb, timeout);
    } else {
      throw std::runtime_error("Socket not initialized");
    }
  }

  void read(char* dest,
            size_t max_len,
            const ReadCallback& cb,
            std::chrono::milliseconds timeout) override {
    if (ssl_socket_) {
      read_impl(ssl_socket_, dest, max_len, cb, timeout);
    } else if (tcp_socket_) {
      read_impl(tcp_socket_, dest, max_len, cb, timeout);
    } else {
      throw std::runtime_error("Socket not initialized");
    }
  }

  bool good() override {
    if (ssl_socket_) {
      return ssl_socket_->lowest_layer().is_open();
    } else if (tcp_socket_) {
      return tcp_socket_->is_open();
    }
    return false;
  }

  void close() override {
    try {
      if (ssl_socket_) {
        ssl_socket_->lowest_layer().close();
      } else if (tcp_socket_) {
        tcp_socket_->close();
      }
    } catch (const std::exception& ex) {}
  }

 private:
  using TcpSocket = boost::asio::ip::tcp::socket;

  void set_timeout(std::chrono::milliseconds from_now) {
    timer_.expires_from_now(from_now);
    timer_.async_wait([=](auto& ec) {
      if (ec != boost::asio::error::operation_aborted) {
        this->close();
      }
    });
  }

  void open_coro(const ConnectCallback& cb,
                 const boost::asio::yield_context& yield) {
    boost::asio::ip::tcp::resolver resolver(*io_service_);
    boost::asio::ip::tcp::resolver::query query(endpoint_,
                                                std::to_string(port_));
    try {
      auto endpoint_iterator = resolver.async_resolve(query, yield);
      if (secure_) {
        boost::asio::async_connect(ssl_socket_->lowest_layer(),
                                   endpoint_iterator,
                                   yield);
        ssl_socket_->async_handshake(boost::asio::ssl::stream_base::client,
                                     yield);
      } else {
        boost::asio::async_connect(*tcp_socket_,
                                   endpoint_iterator,
                                   yield);
      }
      timer_.cancel();
      cb(true, "");
    } catch (const std::exception& e) {
      cb(false, e.what());
    }
  }

  template <typename SocketType>
  void write_impl(const std::shared_ptr<SocketType>& sock,
                  const char* data,
                  size_t len,
                  const WriteCallback& cb,
                  std::chrono::milliseconds timeout) {
    set_timeout(timeout);
    boost::asio::async_write(
        *sock,
        boost::asio::buffer(data, len),
        [=](auto& ec, auto bytes_written) {
          timer_.cancel();
          if (ec) {
            LOG(ERROR) << "Error during socket write: " << ec.message() << "; "
                       << bytes_written << " bytes out of " << len
                       << " written";
            this->close();
            cb(false, ec.message());
          } else {
            cb(true, "");
          }
        });
  }

  // TODO document this, it's quite tricky
  template <typename SocketType>
  void read_impl(const std::shared_ptr<SocketType>& sock,
                 char* dest,
                 size_t max_len,
                 const ReadCallback& cb,
                 std::chrono::milliseconds timeout) {
    auto start = std::chrono::steady_clock::now();
    set_timeout(timeout);
    final_read_callback_ = []{};
    boost::asio::async_read(
        *sock,
        boost::asio::buffer(dest, max_len),
        [=](auto& ec, size_t bytes_transferred) -> size_t {
          if (ec) {
            timer_.cancel();
            LOG(ERROR) << "Error during socket read: " << ec.message() << "; "
                       << bytes_transferred << " bytes read so far";
            cb(-1,
               ec.message(),
               [this](auto final_cb) { final_read_callback_ = final_cb; });
            this->close();
            final_read_callback_();
            return 0;
          } else {
            if (bytes_transferred == 0) {
              return max_len;
            }

            bool read_more = true;
            cb((int) bytes_transferred,
                "",
                [&](auto& final_cb) {
                  read_more = false;
                  final_read_callback_ = final_cb;
                });

            if (read_more) {
              auto elapsed = std::chrono::steady_clock::now() - start;
              auto time_left = timeout -
                  std::chrono::duration_cast<std::chrono::milliseconds>(
                      elapsed);
              this->read_impl(sock, dest, max_len, cb, time_left);
            } else {
              timer_.cancel();
              final_read_callback_();
            }

            return 0;
          }
        },
        [](auto, auto) {});
  }

  std::shared_ptr<boost::asio::io_service> io_service_;
  boost::asio::steady_timer timer_;

  boost::asio::ssl::context ssl_ctx_;
  std::shared_ptr<boost::asio::ssl::stream<TcpSocket>> ssl_socket_;

  std::shared_ptr<TcpSocket> tcp_socket_;

  std::function<void ()> final_read_callback_;

  std::string endpoint_;
  int port_;
  bool secure_;
  bool verify_cert_;
};

class IoServiceSocketFactory : public SocketFactory {
 public:
  IoServiceSocketFactory()
    : io_service_(std::make_shared<boost::asio::io_service>()),
      w_(*io_service_),
      thread_([this] { io_service_->run(); }) {}

  ~IoServiceSocketFactory() {
    w_.~work();
    io_service_->stop();
    thread_.join();
  }

  std::shared_ptr<Socket> create(const std::string& endpoint,
                                 int port,
                                 bool secure,
                                 bool verify_cert) override  {
    return std::make_shared<IoServiceSocket>(io_service_,
                                             endpoint,
                                             port,
                                             secure,
                                             verify_cert);
  }

 private:
  std::shared_ptr<boost::asio::io_service> io_service_;
  boost::asio::io_service::work w_;
  aws::thread thread_;
};

} //namespace http
} //namespace aws

#endif //AWS_HTTP_IO_SERVICE_SOCKET_H_
