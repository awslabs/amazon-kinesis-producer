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

#include <aws/metrics/metrics_manager.h>

namespace aws {
namespace metrics {

namespace detail {

std::shared_ptr<Metric> MetricsFinderBuilder::find() {
  return manager_.get_metric(mf_);
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

  return false;;
}

boost::optional<std::string>
MetricsManager::generate_query_args(const Metric& m,
                                    int idx,
                                    boost::posix_time::ptime tp) {
  std::string prefix("MetricData.member.");
  prefix += std::to_string(idx);
  prefix += ".";

  std::string metric_name;

  std::stringstream ss;

  {
    int i = 1;
    for (auto& p : m.all_dimensions()) {
      if (p.first != "MetricName") {
        std::string dim_prefix("Dimensions.member.");
        dim_prefix += std::to_string(i++);
        dim_prefix += ".";
        ss << prefix << dim_prefix << "Name=" << escape(p.first) << "&"
           << prefix << dim_prefix << "Value=" << escape(p.second) << "&";
      } else {
        ss << prefix << "MetricName=" << p.second << "&";
        metric_name = p.second;
      }
    }
  }

  {
    auto& a = m.accumulator();
    auto count = a.count(kNumBuckets);
    if (count == 0) {
      return boost::none;
    }

    std::string stat_prefix(prefix);
    stat_prefix += "StatisticValues.";
    ss << stat_prefix << "Maximum=" << escape(a.max(kNumBuckets)) << "&"
       << stat_prefix << "Minimum=" << escape(a.min(kNumBuckets)) << "&"
       << stat_prefix << "Sum=" << escape(a.sum(kNumBuckets)) << "&"
       << stat_prefix << "SampleCount=" << escape(count) << "&";
  }

  ss << prefix << "Unit="
     << aws::metrics::constants::unit(metric_name) << "&";

  ss << prefix << "Timestamp="
     << escape(aws::utils::format_ptime(tp)) << "&";

  return ss.str();
}

void MetricsManager::upload() {
  std::vector<std::shared_ptr<Metric>> uploads;

  for (auto& m : metrics_index_.get_all()) {
    if (constants::filter(m->all_dimensions(), level_, granularity_)) {
      uploads.push_back(std::move(m));
    }
  }

  // Sort in reverse order to create a stack we can pop in unreversed order.
  std::sort(uploads.rbegin(), uploads.rend(), MetricsCmp());

  auto tp = boost::posix_time::microsec_clock::universal_time();
  std::string query_str;
  int j = 1;

  while (!uploads.empty()) {
    auto m = std::move(uploads.back());
    uploads.pop_back();

    auto q = generate_query_args(*m, j, tp);

    // If the query args came back as none, that means the metric was empty;
    // just drop it and move on.
    if (!q) {
      continue;
    }
    // If we're still under the limit with the new addition, go ahead and add
    // it. We can drop the metric now that we've used it.
    assert(q->length() < 39 * 1024);
    if (query_str.length() + q->length() < 39 * 1024 && j <= 20) {
      query_str += *q;
      j++;
    // Otherwise, flush, and reinsert the metric back into the stack because
    // we need to change its index to 1 and re-generate the query args.
    } else {
      upload_one_batch(query_str);
      query_str.clear();
      j = 1;
      uploads.push_back(std::move(m));
    }
  }

  // Flush any remaining data.
  if (query_str.length() > 0) {
    upload_one_batch(query_str);
  }
}

void MetricsManager::upload_one_batch(const std::string& query_str) {
  std::stringstream ss;
  ss << "/doc/2010-08-01/?" << query_str
     << "Action=PutMetricData"
     << "&Version=2010-08-01"
     << "&Namespace=" << cw_namespace_;
  upload_with_path(std::make_shared<std::string>(ss.str()));
}

void MetricsManager::upload_with_path(
    const std::shared_ptr<std::string>& path) {
  aws::http::HttpRequest req("POST", *path);
  req.add_header("Host", endpoint_);
  req.add_header("Content-Length", "0");

  aws::auth::SigV4Context ctx(region_, "monitoring", creds_);

  try {
    aws::auth::sign_v4(req, ctx);
    http_client_->put(req,
                      [this](auto& r) { this->handle_result(r); },
                      path);
  } catch (const std::exception& e) {
    handle_result(std::make_shared<aws::http::HttpResult>(e.what(), path));
  }
}

void MetricsManager::handle_result(
    const std::shared_ptr<aws::http::HttpResult>& result) {
  bool retry = false;
  bool failed = false;
  std::string err_msg;

  if (!result->successful()) {
    failed = true;
    err_msg = result->error();
    retry = true;
  } else if (result->status_code() != 200) {
    failed = true;
    err_msg = result->response_body();
    retry = result->status_code() >= 500;
  }

  if (failed) {
    LOG(ERROR) << "Metrics upload failed: \n"
               << err_msg << "\n"
               << "Request was: \n"
               << *result->template context<std::string>();
  }

  if (retry) {
    Lock lk(mutex_);
    retryable_requests_.push_back(result->template context<std::string>());
  }
}

void MetricsManager::MetricsManager::retry_uploads() {
  if (retryable_requests_.empty()) {
    return;
  }

  std::list<std::shared_ptr<std::string>> tmp;
  {
    Lock lk(mutex_);
    std::swap(tmp, retryable_requests_);
  }

  while (!tmp.empty()) {
    auto p = tmp.front();
    tmp.pop_front();
    upload_with_path(p);
  }
}

} //namespace metrics
} //namespace aws
