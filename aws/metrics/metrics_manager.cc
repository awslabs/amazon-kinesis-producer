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

#include <aws/metrics/metrics_header.h>
#include <aws/metrics/metrics_manager.h>

#include <aws/monitoring/model/Dimension.h>
#include <aws/monitoring/model/MetricDatum.h>
#include <aws/monitoring/model/PutMetricDataRequest.h>
#include <aws/monitoring/model/Statistic.h>
#include <aws/monitoring/model/StatisticSet.h>

#define MAX_RETRY_DELAY_SECONDS 60 /* 1 Minute */

namespace aws {
namespace metrics {

namespace detail {

std::shared_ptr<Metric> MetricsFinderBuilder::find() {
  return manager_.get_metric(mf_);
}

bool is_blank(const std::string& str) {
  return std::all_of(str.begin(), str.end(), [](const unsigned char c) {
    return std::isblank(c);
  });
}

Aws::CloudWatch::Model::MetricDatum
to_sdk_metric_datum(const std::shared_ptr<Metric> m, TimePoint begin, TimePoint end) {
  auto& a = m->accumulator();
  Aws::CloudWatch::Model::StatisticSet ss;
  ss.SetSum(a.sum(begin, end));
  ss.SetMinimum(a.min(begin, end));
  ss.SetMaximum(a.max(begin, end));
  ss.SetSampleCount(a.count(begin, end));

  Aws::CloudWatch::Model::MetricDatum d;
  d.SetStatisticValues(std::move(ss));

  for (auto& p : m->all_dimensions()) {
    if (p.first == "MetricName") {
      d.SetMetricName(p.second);
    } else if (!is_blank(p.second)) {
      Aws::CloudWatch::Model::Dimension dim;
      dim.SetName(p.first);
      dim.SetValue(p.second);
      d.AddDimensions(std::move(dim));
    }
  }

  d.SetUnit(Aws::CloudWatch::Model::StandardUnitMapper::GetStandardUnitForName(
      aws::metrics::constants::unit(d.GetMetricName())));


  d.SetTimestamp(Aws::Utils::DateTime::Now());
  return d;
}

} //namespace detail

bool MetricsManager::MetricsCmp::operator()(std::shared_ptr<Metric>& a,
                                            std::shared_ptr<Metric>& b) {
  auto& v1 = a->all_dimensions();
  auto& v2 = b->all_dimensions();

  if (v1.size() != v2.size()) {
    return v1.size() < v2.size();
  }

  for (size_t i = 0; i < v1.size(); i++) {
    if (v1[i] != v2[i]) {
      return v1[i] < v2[i];
    }
  }

  return false;
}

void MetricsManager::upload() {
  std::vector<std::shared_ptr<Metric>> uploads;

  TimePoint begin = upload_checkpoint_;
  TimePoint end = Clock::now() - std::chrono::seconds(1);

  upload_checkpoint_ = end;

  auto metrics = metrics_index_.get_all();

  for (auto& m : metrics) {
    if (constants::filter(m->all_dimensions(), level_, granularity_) &&
        m->accumulator().count(begin, end) > 0) {
      uploads.push_back(m);
    }
  }

  std::sort(uploads.begin(), uploads.end(), MetricsCmp());

  for (size_t i = 1; i <= uploads.size(); i++) {
    size_t step = uploads.size() / i;
    if (step > 20) {
      continue;
    }

    std::vector<Aws::CloudWatch::Model::PutMetricDataRequest> batches;

    for (size_t j = 0; j < uploads.size(); j += step) {
      size_t k = std::min(j + step, uploads.size());
      Aws::CloudWatch::Model::PutMetricDataRequest req;
      req.SetNamespace(cw_namespace_);
      for (size_t z = j; z < k; z++) {
        req.AddMetricData(detail::to_sdk_metric_datum(uploads[z], begin, end));
      }
      batches.emplace_back(req);
    }

    bool small_enough = true;
    for (auto& pmdr : batches) {
      if (pmdr.SerializePayload().size() > 38 * 1024) {
        small_enough = false;
        break;
      }
    }

    if (small_enough) {
      for (auto& pmdr : batches) {
        upload_one(pmdr, std::make_shared<detail::UploadContext>());
      }
      break;
    }
  }

  for (auto& m : metrics) {
    m->accumulator().flush(end);
  }
}

void MetricsManager::upload_one(
    const Aws::CloudWatch::Model::PutMetricDataRequest& pmdr,
    const std::shared_ptr<detail::UploadContext>& ctx) {
  cw_client_->PutMetricDataAsync(
      pmdr,
      [this](auto /*client*/, auto& req, auto& outcome, auto& sdk_ctx) {
        if (!outcome.IsSuccess()) {
          auto ctx = std::dynamic_pointer_cast<detail::UploadContext>(
              std::const_pointer_cast<Aws::Client::AsyncCallerContext>(
                  sdk_ctx));
          auto e = outcome.GetError();
          auto code = e.GetExceptionName();
          auto msg = e.GetMessage();
          LOG(error) << "Metrics upload failed. | Code: " << code
                     << " | Message: " << msg << " | Request was: "
                     << req.SerializePayload();
        }
      },
      ctx);
}

} //namespace metrics
} //namespace aws
