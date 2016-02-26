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
#include <boost/asio/ssl.hpp>
#include <boost/asio/steady_timer.hpp>

#include <aws/utils/logging.h>
#include <aws/utils/utils.h>

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
    connect_cb_ = cb;

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

    resolver_ = std::make_shared<boost::asio::ip::tcp::resolver>(*io_service_);
    boost::asio::ip::tcp::resolver::query query(endpoint_,
                                                std::to_string(port_));
    resolver_->async_resolve(query, [this](auto& ec, auto ep) {
      this->on_resolve(ec, ep);
    });

    last_use_ = std::chrono::steady_clock::now();
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

    last_use_ = std::chrono::steady_clock::now();
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

    last_use_ = std::chrono::steady_clock::now();
  }

  bool good() override {
    bool is_open = false;
    if (ssl_socket_) {
      is_open = ssl_socket_->lowest_layer().is_open();
    } else if (tcp_socket_) {
      is_open = tcp_socket_->is_open();
    }
    bool idle_timed_out_ =
        aws::utils::seconds_since(last_use_) > kIdleTimeoutSeconds;
    return is_open && !idle_timed_out_;
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
  static constexpr const double kIdleTimeoutSeconds = 8;

  using TcpSocket = boost::asio::ip::tcp::socket;

  void set_timeout(std::chrono::milliseconds from_now) {
    timer_.expires_from_now(from_now);
    timer_.async_wait([=](auto& ec) {
      if (ec != boost::asio::error::operation_aborted) {
        this->close();
      }
    });
  }

  void on_resolve(const boost::system::error_code& ec,
                  boost::asio::ip::tcp::resolver::iterator endpoint_it) {
    if (ec) {
      on_connect_finish(ec);
      return;
    }

    if (secure_) {
      boost::asio::async_connect(
          ssl_socket_->lowest_layer(),
          endpoint_it,
          [this](auto& ec, auto ep) { this->on_ssl_connect(ec); });
    } else {
      boost::asio::async_connect(
          *tcp_socket_,
          endpoint_it,
          [this](auto& ec, auto ep) { this->on_connect_finish(ec); });
    }
  }

  void on_ssl_connect(const boost::system::error_code& ec) {
    if (ec) {
      on_connect_finish(ec);
      return;
    }

    SSL_set_tlsext_host_name(ssl_socket_->native_handle(), endpoint_.c_str());

    ssl_socket_->async_handshake(
        boost::asio::ssl::stream_base::client,
        [this](auto& ec) { this->on_connect_finish(ec); });
  }

  void on_connect_finish(const boost::system::error_code& ec) {
    timer_.cancel();
    if (ec) {
      connect_cb_(false, ec.message());
    } else {
      connect_cb_(true, "");
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
            LOG(error) << "Error during socket write: " << ec.message() << "; "
                       << bytes_written << " bytes out of " << len
                       << " written (" << endpoint_ << ":" << port_ << ")";
            this->close();
            cb(false, ec.message());
          } else {
            cb(true, "");
          }
        });
  }

  template <typename SocketType>
  void read_impl(const std::shared_ptr<SocketType>& sock,
                 char* dest,
                 size_t max_len,
                 const ReadCallback& cb,
                 std::chrono::milliseconds timeout) {
    set_timeout(timeout);
    boost::asio::async_read(
        *sock,
        boost::asio::buffer(dest, max_len),
        [=](auto& ec, size_t bytes_transferred) -> size_t {
          if (!ec && bytes_transferred == 0) {
            return max_len;
          } else if ((!ec || ec == boost::asio::error::eof) &&
                     bytes_transferred > 0) {
            cb((int) bytes_transferred, "");
            return 0;
          } else {
            LOG(error) << "Error during socket read: " << ec.message() << "; "
                       << bytes_transferred << " bytes read so far ("
                       << endpoint_ << ":" << port_ << ")";
            this->close();
            cb(-1, ec.message());
            return 0;
          }
        },
        [this](auto, auto) {
          timer_.cancel();
        });
  }

  std::shared_ptr<boost::asio::io_service> io_service_;
  boost::asio::steady_timer timer_;
  boost::asio::ssl::context ssl_ctx_;
  std::shared_ptr<boost::asio::ssl::stream<TcpSocket>> ssl_socket_;
  std::shared_ptr<TcpSocket> tcp_socket_;
  std::string endpoint_;
  int port_;
  bool secure_;
  bool verify_cert_;
  ConnectCallback connect_cb_;
  std::shared_ptr<boost::asio::ip::tcp::resolver> resolver_;
  std::chrono::steady_clock::time_point last_use_;
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
