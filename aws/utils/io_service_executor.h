/*
 * Copyright 2019 Amazon.com, Inc. or its affiliates.
 * Licensed under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef AWS_UTILS_IO_SERVICE_EXECUTOR_H_
#define AWS_UTILS_IO_SERVICE_EXECUTOR_H_

#include <boost/asio/io_context.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/executor_work_guard.hpp>

#include <aws/mutex.h>
#include <aws/utils/executor.h>
#include <aws/utils/concurrent_linked_queue.h>
#include <aws/utils/spin_lock.h>

namespace aws {
namespace utils {

class SteadyTimerScheduledCallback : boost::noncopyable,
                                     public ScheduledCallback {
 public:
  SteadyTimerScheduledCallback(Executor::Func f,
                               boost::asio::io_context& io_ctx,
                               TimePoint at)
      : completed_(false),
        f_(std::move(f)),
        timer_(io_ctx) {
    reschedule(at);
  }

  void cancel() override {
    timer_.cancel();
    completed_ = true;
  }

  bool completed() override {
    return completed_;
  }

  void reschedule(TimePoint at) override {
    completed_ = false;
    timer_.expires_at(at);
    timer_.async_wait([this](auto& ec) {
      if (ec != boost::asio::error::operation_aborted) {
        f_();
      }
      completed_ = true;
    });
  }

  TimePoint expiration() override {
    return timer_.expiry();
  }

 private:
  bool completed_;
  Executor::Func f_;
  boost::asio::steady_timer timer_;
};

class IoServiceExecutor : boost::noncopyable,
                          public Executor {
 public:
  IoServiceExecutor(size_t num_threads)
      : io_context_(std::make_shared<boost::asio::io_context>()),
        work_guard_(boost::asio::make_work_guard(*io_context_)),
        clean_up_cb_(
            [this] { this->clean_up(); },
            *io_context_,
            Clock::now() + std::chrono::seconds(1)) {
    for (size_t i = 0; i < num_threads; i++) {
      threads_.emplace_back([this] { io_context_->run(); });
    }
  }

  ~IoServiceExecutor() {
    work_guard_.reset();
    io_context_->stop();
    for (auto& t : threads_) {
      t.join();
    }
    clean_up();
  }

  void submit(Func f) override {
    boost::asio::post(*io_context_, std::move(f));
  };

  std::shared_ptr<ScheduledCallback> schedule(Func f,
                                              TimePoint at) override {
    auto cb =
      std::make_shared<SteadyTimerScheduledCallback>(
          std::move(f),
          *io_context_,
          at);
    callbacks_clq_.put(cb);
    return cb;
  };

  const std::shared_ptr<boost::asio::io_context>& io_context() {
    return io_context_;
  }

  operator boost::asio::io_context&() {
    return *io_context_;
  }

  size_t num_threads() const noexcept override {
    return threads_.size();
  }

  void join() override {
    io_context_->run();
  }

 private:
  using CbPtr = std::shared_ptr<SteadyTimerScheduledCallback>;

  void clean_up() {
    if (!clean_up_mutex_.try_lock()) {
      return;
    }

    CbPtr p;
    while (callbacks_clq_.try_take(p)) {
      if (!p->completed()) {
        callbacks_.push_back(std::move(p));
      }
    }

    for (auto it = callbacks_.begin(); it != callbacks_.end();) {
      if ((*it)->completed()) {
        it = callbacks_.erase(it);
      } else {
        it++;
      }
    }

    clean_up_mutex_.unlock();

    clean_up_cb_.reschedule(Clock::now() + std::chrono::seconds(1));
  }

  std::shared_ptr<boost::asio::io_context> io_context_;
  boost::asio::executor_work_guard<boost::asio::io_context::executor_type> work_guard_;
  std::vector<aws::thread> threads_;
  std::list<CbPtr> callbacks_;
  aws::utils::SpinLock clean_up_mutex_;
  aws::utils::ConcurrentLinkedQueue<CbPtr> callbacks_clq_;
  SteadyTimerScheduledCallback clean_up_cb_;
};

} //namespace utils
} //namespace aws

#endif //AWS_UTILS_IO_SERVICE_EXECUTOR_H_
