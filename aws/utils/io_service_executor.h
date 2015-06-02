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

#ifndef AWS_UTILS_IO_SERVICE_EXECUTOR_H_
#define AWS_UTILS_IO_SERVICE_EXECUTOR_H_

#include <boost/asio/steady_timer.hpp>

#include <aws/utils/executor.h>
#include <aws/mutex.h>

namespace aws {
namespace utils {

class SteadyTimerScheduledCallback : boost::noncopyable,
                                     public ScheduledCallback {
 public:
  SteadyTimerScheduledCallback(Executor::Func f,
                               boost::asio::io_service& io_svc,
                               TimePoint at,
                               bool self_destruct = false)
      : completed_(false),
        f_(std::move(f)),
        timer_(io_svc, at),
        self_destruct_(self_destruct) {
    schedule();
  }

  void cancel() override {
    timer_.expires_at(std::chrono::steady_clock::now());
    completed_ = true;
  }

  bool completed() override {
    return completed_;
  }

  void reschedule(TimePoint at) override {
    timer_.expires_at(at);
    schedule();
  }

  TimePoint expiration() override {
    return timer_.expires_at();
  }

 private:
  void schedule() {
    completed_ = false;

    timer_.async_wait([this](auto& ec) {
      if (ec != boost::asio::error::operation_aborted) {
        f_();
      }
      completed_ = true;
      if (self_destruct_) {
        delete this;
      }
    });
  }

  bool completed_;
  Executor::Func f_;
  boost::asio::steady_timer timer_;
  bool self_destruct_;
};

class IoServiceExecutor : boost::noncopyable,
                          public Executor {
 public:
  IoServiceExecutor(size_t num_threads)
      : io_service_(std::make_shared<boost::asio::io_service>()),
        w_(*io_service_) {
    for (size_t i = 0; i < num_threads; i++) {
      threads_.emplace_back([this] { io_service_->run(); });
    }
  }

  ~IoServiceExecutor() {
    w_.~work();
    io_service_->stop();
    for (auto& t : threads_) {
      t.join();
    }
  }

  void submit(Func f) override {
    io_service_->post(std::move(f));
  };

  void schedule(Func f,
                TimePoint at,
                std::shared_ptr<ScheduledCallback>* container) override {
    if (container != nullptr) {
      *container =
          std::make_shared<SteadyTimerScheduledCallback>(
              std::move(f),
              *io_service_,
              at);
    } else {
      new SteadyTimerScheduledCallback(std::move(f), *io_service_, at, true);
    }
  };

  const std::shared_ptr<boost::asio::io_service>& io_service() {
    return io_service_;
  }

  operator boost::asio::io_service&() {
    return *io_service_;
  }

  size_t num_threads() const noexcept override {
    return threads_.size();
  }

  void join() override {
    io_service_->run();
  }

 private:
  std::shared_ptr<boost::asio::io_service> io_service_;
  boost::asio::io_service::work w_;
  std::vector<aws::thread> threads_;
};

} //namespace utils
} //namespace aws

#endif //AWS_UTILS_IO_SERVICE_EXECUTOR_H_
