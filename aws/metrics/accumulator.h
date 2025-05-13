/*
 * Copyright 2025 Amazon.com, Inc. or its affiliates.
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

#ifndef AWS_METRICS_ACCUMULATOR_H_
#define AWS_METRICS_ACCUMULATOR_H_

#include <list>

#include <boost/accumulators/accumulators.hpp>
#include <boost/accumulators/statistics/stats.hpp>
#include <boost/accumulators/statistics/mean.hpp>
#include <boost/accumulators/statistics/min.hpp>
#include <boost/accumulators/statistics/max.hpp>
#include <boost/accumulators/statistics/sum.hpp>
#include <boost/accumulators/statistics/count.hpp>

#include <aws/metrics/metrics_header.h>
#include <aws/utils/logging.h>

#include <aws/mutex.h>

// Bucketed time-series accumulator modeled after CloudWatch metrics/statistics.
namespace aws {
namespace metrics {

namespace detail {

using Mutex = aws::shared_mutex;
using ReadLock = aws::shared_lock<Mutex>;
using WriteLock = aws::unique_lock<Mutex>;

template <typename AccumType>
class ConcurrentAccumulator {
 public:
  template <typename V>
  void operator()(V&& val) {
    WriteLock lk(mutex_);
    accum_(std::forward<decltype(val)>(val));
  }

  template <typename Stat>
  decltype(auto) get() {
    ReadLock lk(mutex_);
    return boost::accumulators::extract_result<Stat>(accum_);
  }

 private:
  Mutex mutex_;
  AccumType accum_;
};

template <typename ValType,
          typename AccumType,
          typename BucketSize,
          size_t NumBuckets>
class AccumulatorList {
 public:
  void operator()(ValType val) {
    auto tp = current_time();

    {
      ReadLock lk(mutex_);
      if (!accums_.empty() && accums_.back().first == tp) {
        (accums_.back().second)(val);
        return;
      }
    }

    WriteLock lk(mutex_);
    if (accums_.empty() || accums_.back().first < tp) {
      accums_.emplace_back(std::piecewise_construct,
                           std::forward_as_tuple(tp),
                           std::forward_as_tuple());
    }
    (accums_.back().second)(val);
  }

  template <typename Stat>
  ValType get(size_t buckets) {
    if (buckets == 0) {
      return 0;
    }

    auto r = range(buckets);
    return get<Stat>(r.first, r.second);
  }

  template <typename Stat>
  ValType get(TimePoint begin, TimePoint end) {
    ReadLock lk(mutex_);

    if (std::is_same<Stat, boost::accumulators::tag::count>::value ||
        std::is_same<Stat, boost::accumulators::tag::sum>::value) {
      return this->sum<Stat>(begin, end);
    } else if (std::is_same<Stat, boost::accumulators::tag::mean>::value) {
      return
          this->sum<boost::accumulators::tag::sum>(begin, end) /
              this->sum<boost::accumulators::tag::count>(begin, end);
    } else {
      return
          boost::accumulators::extract_result<Stat>(
              combine<OneStatAccum<Stat>, Stat>(begin, end));
    }
  }

  void flush(TimePoint checkpoint) {
    WriteLock lk(mutex_);
    while (!accums_.empty() && accums_.front().first <= checkpoint) {
      accums_.pop_front();
    }
  }

 private:
  template <typename Stat>
  using OneStatAccum =
      boost::accumulators::accumulator_set<
          ValType,
          boost::accumulators::stats<Stat>>;

  static inline std::pair<TimePoint, TimePoint> range(size_t buckets) {
    return range(buckets, current_time());
  }

  static inline std::pair<TimePoint, TimePoint> range(size_t buckets, TimePoint end) {
    return std::pair<TimePoint, TimePoint>(end - BucketSize(buckets), end);
  }

  static inline TimePoint current_time() {
    return TimePoint(buckets_since_epoch());
  }

  static inline BucketSize buckets_since_epoch() {
    return std::chrono::duration_cast<BucketSize>(
        Clock::now().time_since_epoch());
  }

  static inline size_t buckets_between(TimePoint a, TimePoint b) {
    auto d = (b > a) ? (b - a) : (a - b);
    return std::chrono::duration_cast<BucketSize>(d).count();
  }

  template <typename CombiningAccumType, typename Stat>
  CombiningAccumType combine(TimePoint begin, TimePoint end) {
    CombiningAccumType a;
    for (auto& p : accums_) {
      if (p.first > begin && p.first <= end) {
        a(p.second.template get<Stat>());
      }
    }
    return a;
  }

  template <typename Stat>
  ValType sum(TimePoint begin, TimePoint end) {
    return boost::accumulators::sum(
        combine<OneStatAccum<boost::accumulators::tag::sum>, Stat>(begin, end));
  }

  Mutex mutex_;
  std::list<std::pair<TimePoint, ConcurrentAccumulator<AccumType>>> accums_;
};


template <typename ValType, typename BucketSize, size_t NumBuckets>
class AccumulatorImpl {
 public:
  AccumulatorImpl() : start_time_(Clock::now()) {}

  void operator()(ValType val) {
    put(val);
  }

  void put(ValType val) {
    accums_(val);
    overall_(val);
  }

  ValType count(size_t buckets = SIZE_MAX) {
    return get<boost::accumulators::tag::count>(buckets);
  }

  ValType mean(size_t buckets = SIZE_MAX) {
    return get<boost::accumulators::tag::mean>(buckets);
  }

  ValType min(size_t buckets = SIZE_MAX) {
    return get<boost::accumulators::tag::min>(buckets);
  }

  ValType max(size_t buckets = SIZE_MAX) {
    return get<boost::accumulators::tag::max>(buckets);
  }

  ValType sum(size_t buckets = SIZE_MAX) {
    return get<boost::accumulators::tag::sum>(buckets);
  }

  ValType count(TimePoint begin, TimePoint end) {
    return get<boost::accumulators::tag::count>(begin, end);
  }

  ValType mean(TimePoint begin, TimePoint end) {
    return get<boost::accumulators::tag::mean>(begin, end);
  }

  ValType min(TimePoint begin, TimePoint end) {
    return get<boost::accumulators::tag::min>(begin, end);
  }

  ValType max(TimePoint begin, TimePoint end) {
    return get<boost::accumulators::tag::max>(begin, end);
  }

  ValType sum(TimePoint begin, TimePoint end) {
    return get<boost::accumulators::tag::sum>(begin, end);
  }

  void flush(TimePoint checkpoint) {
    accums_.flush(checkpoint);
  }

  TimePoint start_time() const noexcept {
    return start_time_;
  }

  template <typename TimeUnit>
  uint64_t elapsed() const noexcept {
    return
        std::chrono::duration_cast<TimeUnit>(Clock::now() - start_time_)
            .count();
  }

 private:
  using Accum =
      boost::accumulators::accumulator_set<
          ValType,
          boost::accumulators::stats<boost::accumulators::tag::count,
                                     boost::accumulators::tag::mean,
                                     boost::accumulators::tag::min,
                                     boost::accumulators::tag::max,
                                     boost::accumulators::tag::sum>>;

  template <typename Stat>
  ValType get(size_t buckets) {
    if (buckets != SIZE_MAX) {
      return accums_.template get<Stat>(buckets);
    } else {
      return overall_.template get<Stat>();
    }
  }

  template <typename Stat>
  ValType get(TimePoint begin, TimePoint end) {
    return accums_.template get<Stat>(begin, end);
  }

  detail::AccumulatorList<ValType, Accum, BucketSize, NumBuckets> accums_;
  detail::ConcurrentAccumulator<Accum> overall_;
  TimePoint start_time_;
};

} //namespace detail

using Accumulator = detail::AccumulatorImpl<double, std::chrono::seconds, 60>;

} //namespace metrics
} //namespace aws

#endif //AWS_METRICS_ACCUMULATOR_H_
