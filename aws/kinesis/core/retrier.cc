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

#include <aws/kinesis/core/retrier.h>

#include <aws/utils/json.h>

namespace aws {
namespace kinesis {
namespace core {

namespace detail {

MetricsPutter& MetricsPutter::operator ()(
      std::string name,
      double val,
      boost::optional<uint64_t> shard_id,
      boost::optional<std::string> err_code) {
  auto f = metrics_manager_->finder().set_name(name);
  if (err_code) {
    f.set_error_code(*err_code);
  }
  f.set_stream(stream_);
  if (shard_id) {
    f.set_shard(ShardMap::shard_id_to_str(*shard_id));
  }
  f.find()->put(val);
  return *this;
}

} //namespace detail

void Retrier::handle_put_records_result(const Result& result) {
  emit_metrics(result);

  try {
    if (result->successful()) {
      auto status_code = result->status_code();
      if (status_code == 200) {
        on_200(result);
      } else if (status_code >= 500 && status_code < 600) {
        retry_not_expired(result);
      } else {
        // For PutRecords, errors that apply to individual kinesis records
        // (like throttling, too big or bad format) come back in code 200s.
        // This is different from plain old PutRecord, where those come back
        // with code 400. As such, all the errors we want to retry on are
        // handled in the 200 case. All 400 codes are therefore not retryable.
        LOG(error) << "PutRecords failed: " << result->response_body();
        fail(result);
      }
    } else {
      retry_not_expired(result);
    }
  } catch (const std::exception& ex) {
    LOG(error) << "Unexpected error encountered processing http result: "
               << ex.what();
    fail(result,
         "Unexpected Error",
         ex.what());
  }
}

void Retrier::on_200(const Result& result) {
  using aws::metrics::constants::Names;

  auto json = aws::utils::Json(result->response_body());
  auto records = json["Records"];
  auto prr = result->template context<PutRecordsRequest>();
  detail::MetricsPutter metrics_putter(metrics_manager_, result);

  // If somehow there's a size mismatch, subsequent code may crash from
  // array out of bounds, so we're going to explicitly catch it here and
  // print a nicer message. Also, if there's a size mismatch, we can no longer
  // be sure which result is for which record, so we better fail all of them.
  // None of this is expected to happen if the backend behaves correctly,
  // but if it does happen, this will make it easier to identify the problem.
  if (records.size() != prr->size()) {
    std::stringstream ss;
    ss << "Count of records in PutRecords response differs from the number "
       << "sent: " << records.size() << "received, but " << prr->size()
       << " were sent.";
    LOG(error) << ss.str();
    fail(result, "Record Count Mismatch", ss.str());
    return;
  }

  for (size_t i = 0; i < prr->size(); i++) {
    auto record = records[i];
    auto& kr = prr->items()[i];
    bool success = record["SequenceNumber"];
    auto start = result->start_time();
    auto end = result->end_time();

    auto shard_id = kr->items().front()->predicted_shard();
    if (success) {
      metrics_putter
          (Names::KinesisRecordsPut, 1, shard_id)
          (Names::KinesisRecordsDataPut, kr->accurate_size(), shard_id)
          (Names::AllErrors, 0, shard_id);
    } else {
      metrics_putter
          (Names::KinesisRecordsPut, 0, shard_id)
          (Names::ErrorsByCode, 1, shard_id, (std::string) record["ErrorCode"])
          (Names::AllErrors, 1, shard_id);
    }

    if (success) {
      for (auto& ur : kr->items()) {
        succeed_if_correct_shard(ur,
                                 start,
                                 end,
                                 record["ShardId"],
                                 record["SequenceNumber"]);
      }
    } else {
      std::string err_code = record["ErrorCode"];
      std::string err_msg = record["ErrorMessage"];

      bool can_retry =
          (!config_->fail_if_throttled() &&
           err_code == "ProvisionedThroughputExceededException") ||
          (err_code == "InternalFailure") ||
          (err_code == "ServiceUnavailable");

      if (can_retry) {
        retry_not_expired(kr, start, end, err_code, err_msg);
      } else {
        fail(kr, start, end, err_code, err_msg);
      }
    }
  }
}

void Retrier::retry_not_expired(const Result& result) {
  retry_not_expired(
      result,
      result->successful()
          ? std::to_string(result->status_code())
          : "Exception",
      result->successful()
          ? result->response_body().substr(0, 4096)
          : result->error());
}

void Retrier::retry_not_expired(const Result& result,
                                const std::string& err_code,
                                const std::string& err_msg) {
  for (auto& kr : result->template context<PutRecordsRequest>()->items()) {
    retry_not_expired(kr,
                      result->start_time(),
                      result->end_time(),
                      err_code,
                      err_msg);
  }
}

void Retrier::retry_not_expired(const std::shared_ptr<KinesisRecord>& kr,
                                TimePoint start,
                                TimePoint end,
                                const std::string& err_code,
                                const std::string& err_msg) {
  for (auto& ur : kr->items()) {
    retry_not_expired(ur, start, end, err_code, err_msg);
  }
}

void Retrier::retry_not_expired(const std::shared_ptr<UserRecord>& ur,
                                TimePoint start,
                                TimePoint end,
                                const std::string& err_code,
                                const std::string& err_msg) {
  ur->add_attempt(
      Attempt()
          .set_start(start)
          .set_end(end)
          .set_error(err_code, err_msg));

  if (ur->expired()) {
    fail(ur,
         std::chrono::steady_clock::now(),
         std::chrono::steady_clock::now(),
         "Expired",
         "Record has reached expiration");
  } else {
    // TimeSensitive automatically sets the deadline to the expiration if
    // the given deadline is later than the expiration.
    ur->set_deadline_from_now(
        std::chrono::milliseconds(
            config_->record_max_buffered_time() / 2));
    retry_cb_(ur);
  }
}

void Retrier::fail(const Result& result) {
  fail(result,
       result->successful()
          ? std::to_string(result->status_code())
          : "Exception",
       result->successful()
          ? result->response_body().substr(0, 4096)
          : result->error());
}

void Retrier::fail(const Result& result,
                   const std::string& err_code,
                   const std::string& err_msg) {
  for (auto& kr : result->template context<PutRecordsRequest>()->items()) {
    fail(kr,
         result->start_time(),
         result->end_time(),
         err_code,
         err_msg);
  }
}

void Retrier::fail(const std::shared_ptr<KinesisRecord>& kr,
                   TimePoint start,
                   TimePoint end,
                   const std::string& err_code,
                   const std::string& err_msg) {
  for (auto& ur : kr->items()) {
    fail(ur, start, end, err_code, err_msg);
  }
}

void Retrier::fail(const std::shared_ptr<UserRecord>& ur,
                   TimePoint start,
                   TimePoint end,
                   const std::string& err_code,
                   const std::string& err_msg) {
  finish_user_record(
      ur,
      Attempt()
          .set_start(start)
          .set_end(end)
          .set_error(err_code, err_msg));
}

void Retrier::succeed_if_correct_shard(const std::shared_ptr<UserRecord>& ur,
                                       TimePoint start,
                                       TimePoint end,
                                       const std::string& shard_id,
                                       const std::string& sequence_number) {
  if (ur->predicted_shard() &&
      *ur->predicted_shard() != ShardMap::shard_id_from_str(shard_id)) {
    LOG(warning) << "Record went to shard " << shard_id << " instead of the "
                 << "prediceted shard " << *ur->predicted_shard() << "; this "
                 << "usually means the sharp map has changed.";
    shard_map_invalidate_cb_(start);

    retry_not_expired(ur,
                      start,
                      end,
                      "Wrong Shard",
                      "Record did not end up in expected shard.");
  } else {
    finish_user_record(
        ur,
        Attempt()
            .set_start(start)
            .set_end(end)
            .set_result(shard_id, sequence_number));
  }
}

void Retrier::finish_user_record(const std::shared_ptr<UserRecord>& ur,
                                 const Attempt& final_attempt) {
  if (!ur->finished()) {
    ur->add_attempt(final_attempt);
    emit_metrics(ur);
    finish_cb_(ur);
  }
}

void Retrier::emit_metrics(const Result& result) {
  using aws::metrics::constants::Names;

  detail::MetricsPutter metrics_putter(metrics_manager_, result);
  auto prr = result->template context<PutRecordsRequest>();

  double num_urs = 0;
  for (auto& kr : prr->items()) {
    metrics_putter(Names::UserRecordsPerKinesisRecord,
                   kr->items().size(),
                   kr->items().front()->predicted_shard());
    num_urs += kr->items().size();
  }

  metrics_putter
      (Names::RequestTime, result->duration_millis())
      (Names::KinesisRecordsPerPutRecordsRequest, prr->items().size())
      (Names::UserRecordsPerPutRecordsRequest, num_urs);

  boost::optional<std::string> err_code;
  if (result->successful()) {
    auto status_code = result->status_code();
    if (status_code != 200) {
      // TODO parse the json (if any) to get the error code
      err_code = "Http" + std::to_string(status_code);
    }
  } else {
    err_code = result->error().substr(0, 255);
  }

  if (err_code) {
    metrics_putter
        (Names::ErrorsByCode, 1, boost::none, err_code)
        (Names::AllErrors, 1);
  }
}

void Retrier::emit_metrics(const std::shared_ptr<UserRecord>& ur) {
  using aws::metrics::constants::Names;

  assert(ur->attempts().size() > 0);

  bool successful = ur->attempts().back();

  std::string shard_id;
  if (successful) {
    shard_id = ur->attempts().back().shard_id();
  } else if (ur->predicted_shard()) {
    shard_id = ShardMap::shard_id_to_str(*ur->predicted_shard());
  }

  auto put = [&](auto name, auto val) {
    auto f = metrics_manager_->finder().set_name(name)
                                       .set_stream(ur->stream());
    if (!shard_id.empty()) {
      f.set_shard(shard_id);
    }
    f.find()->put(val);
  };

  if (successful) {
    put(Names::UserRecordsPut, 1);
    put(Names::UserRecordsDataPut, ur->data().length());
  } else {
    put(Names::UserRecordsPut, 0);
    if (ur->attempts().back().error_code() == "Expired") {
      put(Names::UserRecordExpired, 1);
    }
  }

  auto last = ur->arrival();
  for (auto& a : ur->attempts()) {
    put(Names::BufferingTime,
        std::chrono::duration_cast<std::chrono::milliseconds>(
            a.start() - last).count());
    last = a.end();
  }

  put(Names::RetriesPerRecord, ur->attempts().size() - 1);
}

} //namespace core
} //namespace kinesis
} //namespace aws
