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

#ifndef AWS_HTTP_HTTP_CLIENT_2_H_
#define AWS_HTTP_HTTP_CLIENT_2_H_

#include <array>
#include <atomic>
#include <list>
#include <mutex>
#include <queue>

#include <glog/logging.h>

#include <aws/mutex.h>
#include <aws/http/socket.h>
#include <aws/http/http_result.h>
#include <aws/http/http_request.h>
#include <aws/utils/executor.h>

namespace aws {
namespace http {

using Clock = std::chrono::steady_clock;
using TimePoint = Clock::time_point;
using ResponseCallback =
    std::function<void (const std::shared_ptr<HttpResult>&)>;

namespace detail {

class Task : boost::noncopyable {
 public:
  using SocketReturn = std::function<void (const std::shared_ptr<Socket>&)>;

  Task(HttpRequest& request,
       const std::shared_ptr<void>& context,
       const ResponseCallback& response_cb,
       const SocketReturn& socket_return,
       TimePoint start_deadline,
       std::chrono::milliseconds timeout)
      : raw_request_(request.to_string()),
        context_(context),
        response_cb_(response_cb),
        socket_return_(socket_return),
        start_deadline_(start_deadline),
        timeout_(timeout),
        response_(std::make_unique<HttpResponse>()),
        finished_(false) {}

  void run(const std::shared_ptr<Socket>& socket);

  bool finished() {
    return finished_;
  }

  TimePoint deadline() {
    return start_deadline_;
  }

 private:
  void write();

  void read();

  void fail(std::string reason);

  void succeed();

  void finish(std::shared_ptr<HttpResult> result, bool close_socket = false);

  std::string raw_request_;
  std::shared_ptr<void> context_;
  ResponseCallback response_cb_;
  SocketReturn socket_return_;
  TimePoint start_deadline_;
  std::chrono::milliseconds timeout_;
  std::unique_ptr<HttpResponse> response_;

  TimePoint end_deadline_;
  TimePoint start_;
  std::shared_ptr<Socket> socket_;
  bool finished_;
  std::array<char, 64 * 1024> buffer_;
  std::atomic_flag started_ = ATOMIC_FLAG_INIT;
};

} //namespace detail

class HttpClient {
 public:
  using Millis = std::chrono::milliseconds;

  HttpClient(const std::shared_ptr<aws::utils::Executor>& executor,
             const std::shared_ptr<SocketFactory>& socket_factory,
             const std::string& endpoint,
             int port = 443,
             bool secure = true,
             bool verify_cert = true,
             size_t min_connections = 1,
             size_t max_connections = 6,
             Millis connect_timeout = Millis(10000),
             Millis request_timeout = Millis(10000),
             bool single_use_sockets = false)
      : executor_(executor),
        socket_factory_(socket_factory),
        endpoint_(endpoint),
        port_(port),
        secure_(secure),
        verify_cert_(verify_cert),
        min_connections_(min_connections),
        max_connections_(max_connections),
        connect_timeout_(connect_timeout),
        request_timeout_(request_timeout),
        single_use_sockets_(single_use_sockets),
        /*task_queue_([](auto& a, auto& b) {
          return a->deadline() > b->deadline();
        }),*/
        task_queue_(TaskCmp()),
        pending_(0) {
    std::chrono::milliseconds poll_delay(3000);
    executor_->schedule(
        [=] {
          this->run_tasks();
          scheduled_callback_->reschedule(poll_delay);
        },
        poll_delay,
        &scheduled_callback_);
    open_connections(min_connections);
  }

  size_t available_connections() const noexcept {
    return available_.size();
  }

  size_t total_connections() const noexcept {
    return available_.size() + pending_;
  }

  void put(HttpRequest& request,
           const ResponseCallback& cb = [](auto&) {},
           const std::shared_ptr<void>& context = std::shared_ptr<void>(),
           TimePoint deadline = Clock::now());

 private:
  using Mutex = aws::recursive_mutex;
  using Lock = aws::lock_guard<Mutex>;
  using TaskPtr = std::shared_ptr<detail::Task>;
  // Bug in gcc 5.10 prevents the use of the std::function
  // using TaskCmp = std::function<bool (const TaskPtr& a, const TaskPtr& b)>;
  struct TaskCmp {
    bool operator()(const TaskPtr& a, const TaskPtr& b) {
      return a->deadline() > b->deadline();
    }
  };

  void open_connection();

  void open_connections(size_t n);

  void reuse_socket(const std::shared_ptr<Socket>& socket);

  void run_tasks();

  Mutex mutex_;

  std::shared_ptr<aws::utils::Executor> executor_;
  std::shared_ptr<SocketFactory> socket_factory_;
  std::string endpoint_;
  int port_;
  bool secure_;
  bool verify_cert_;
  size_t min_connections_;
  size_t max_connections_;
  Millis connect_timeout_;
  Millis request_timeout_;
  bool single_use_sockets_;

  std::priority_queue<TaskPtr, std::vector<TaskPtr>, TaskCmp> task_queue_;
  std::list<TaskPtr> tasks_in_progress_;
  std::list<std::shared_ptr<Socket>> available_;
  std::atomic<size_t> pending_;
  std::shared_ptr<aws::utils::ScheduledCallback> scheduled_callback_;
};

} //namespace http
} //namespace aws

#endif //AWS_HTTP_HTTP_CLIENT_2_H_
