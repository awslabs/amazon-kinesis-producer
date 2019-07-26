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

#include <boost/asio/steady_timer.hpp>

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
                               boost::asio::io_service& io_svc,
                               TimePoint at)
      : completed_(false),
        f_(std::move(f)),
        timer_(io_svc) {
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
    return timer_.expires_at();
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
      : io_service_(std::make_shared<boost::asio::io_service>()),
        w_(*io_service_),
        clean_up_cb_(
            [this] { this->clean_up(); },
            *io_service_,
            Clock::now() + std::chrono::seconds(1)) {
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
    clean_up();
  }

  void submit(Func f) override {
    io_service_->post(std::move(f));
  };

  std::shared_ptr<ScheduledCallback> schedule(Func f,
                                              TimePoint at) override {
    auto cb =
      std::make_shared<SteadyTimerScheduledCallback>(
          std::move(f),
          *io_service_,
          at);
    callbacks_clq_.put(cb);
    return cb;
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

  std::shared_ptr<boost::asio::io_service> io_service_;
  boost::asio::io_service::work w_;
  std::vector<aws::thread> threads_;
  std::list<CbPtr> callbacks_;
  aws::utils::SpinLock clean_up_mutex_;
  aws::utils::ConcurrentLinkedQueue<CbPtr> callbacks_clq_;
  SteadyTimerScheduledCallback clean_up_cb_;
};

} //namespace utils
} //namespace aws

#endif //AWS_UTILS_IO_SERVICE_EXECUTOR_H_
