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

#include <aws/http/http_client.h>

#include <boost/algorithm/string/case_conv.hpp>

namespace aws {
namespace http {

namespace detail {

void Task::run(const std::shared_ptr<Socket>& socket) {
  if (!started_.test_and_set()) {
    socket_ = socket;
    start_ = Clock::now();
    end_deadline_ = Clock::now() + timeout_,
    write();
  } else {
    throw std::runtime_error("The same task cannot be run more than once");
  }
}

void Task::write() {
  socket_->write(
      raw_request_.data(),
      raw_request_.length(),
      [this](bool success, const std::string& reason) {
        if (success) {
          this->read();
        } else {
          this->fail(reason);
        }
      },
      timeout_);
}

void Task::read() {
  socket_->read(
      buffer_.data(),
      buffer_.size(),
      [this](int num_read, const std::string& reason) {
        if (num_read < 0) {
          this->fail(reason);
          return;
        }

        response_->update(buffer_.data(), num_read);

        if (response_->complete()) {
          this->succeed();
        } else {
          this->read();
        }
      },
      std::chrono::duration_cast<std::chrono::milliseconds>(
          end_deadline_ - Clock::now()));
}

void Task::fail(std::string reason) {
  finish(
      std::make_shared<HttpResult>(
          std::move(reason),
          std::move(context_),
          start_,
          Clock::now()),
      true);
}

void Task::succeed() {
  bool close_socket = false;
  for (auto h : response_->headers()) {
    if (boost::to_lower_copy(h.first) == "connection" &&
        boost::to_lower_copy(h.second) == "close") {
      close_socket = true;
      break;
    }
  }

  finish(
      std::make_shared<HttpResult>(
          std::move(response_),
          std::move(context_),
          start_,
          Clock::now()),
      close_socket);
}

void Task::finish(std::shared_ptr<HttpResult> result, bool close_socket) {
  started_.test_and_set();
  if (!submitted_.test_and_set()) {
    executor_->submit([=] {
      response_cb_(result);
      if (socket_) {
        if (close_socket) {
          socket_->close();
        }
        socket_return_(socket_);
      }
      finished_ = true;
    });
  }
}

} //namespace detail

void HttpClient::put(HttpRequest& request,
                     const ResponseCallback& cb ,
                     const std::shared_ptr<void>& context ,
                     TimePoint deadline,
                     TimePoint expiration) {
  Lock lk(mutex_);
  task_queue_.insert(
      std::make_shared<detail::Task>(
          executor_,
          request,
          context,
          cb,
          [this](auto& socket) { this->reuse_socket(socket); },
          deadline,
          expiration,
          request_timeout_));
  run_tasks();
}

void HttpClient::open_connection() {
  pending_++;
  auto s = socket_factory_->create(endpoint_, port_, secure_, verify_cert_);
  s->open(
      [=](bool success, auto& reason) mutable {
        pending_--;
        if (success) {
          Lock lk(mutex_);
          available_.push_back(s);
          this->run_tasks();
        } else {
          if (endpoint_ != "169.254.169.254") {
            LOG(error) << "Failed to open connection to "
                       << endpoint_ << ":" << port_ << " : " << reason;
          }
        }
        // We need to explicitly reset because there is a circular reference
        // between s and this callback.
        s.reset();
      },
      connect_timeout_);
}

void HttpClient::open_connections(size_t n) {
  for (size_t i = 0; i < n && total_connections() < max_connections_; i++) {
    open_connection();
  }
}

void HttpClient::reuse_socket(const std::shared_ptr<Socket>& socket) {
  Lock lk(mutex_);
  pending_--;
  if (!single_use_sockets_ && socket->good()) {
    available_.push_back(socket);
  } else {
    socket->close();
    if (total_connections() < min_connections_) {
      open_connections(min_connections_ - total_connections());
    }
  }
  run_tasks();
}

void HttpClient::run_tasks() {
  Lock lk(mutex_);

  while (!tasks_in_progress_.empty() &&
         tasks_in_progress_.front()->finished()) {
    tasks_in_progress_.pop_front();
  }

  while (!available_.empty() && !available_.front()->good()) {
    available_.pop_front();
  }

  task_queue_.consume_expired([this](const auto& t) {
    tasks_in_progress_.push_back(t);
    t->fail("Expired while waiting in HttpClient queue");
  });

  task_queue_.consume_by_deadline([this](const auto& t) {
    if (available_.empty()) {
      return false;
    }

    pending_++;
    auto socket = available_.front();
    available_.pop_front();
    tasks_in_progress_.push_back(t);
    t->run(socket);
    return true;
  });

  if (task_queue_.size() > total_connections()) {
    open_connections(1);
  }
}

} //namespace http
} //namespace aws
