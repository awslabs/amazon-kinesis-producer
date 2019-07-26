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
#include <unordered_set>

#include <aws/kinesis/core/retrier.h>

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

void
Retrier::handle_put_records_result(std::shared_ptr<PutRecordsContext> prc) {
  auto outcome = prc->get_outcome();
  auto start = prc->get_start();
  auto end = prc->get_end();


  if (!outcome.IsSuccess()) {
    auto e = outcome.GetError();
    auto code = e.GetExceptionName();
    auto msg = e.GetMessage();
    for (auto& kr : prc->get_records()) {
      if (!(config_->fail_if_throttled() &&
            code == "ProvisionedThroughputExceededException")) {
        retry_not_expired(kr, start, end, code, msg);
      } else {
        fail(kr, start, end, code, msg);
      }
    }
  } else {
    detail::MetricsPutter metrics_putter(metrics_manager_, prc->get_stream());
    auto result = outcome.GetResult().GetRecords();

    // If somehow there's a size mismatch, subsequent code may crash from
    // array out of bounds, so we're going to explicitly catch it here and
    // print a nicer message. Also, if there's a size mismatch, we can no longer
    // be sure which result is for which record, so we better fail all of them.
    // None of this is expected to happen if the backend behaves correctly,
    // but if it does happen, this will make it easier to identify the problem.
    if (result.size() != prc->get_records().size()) {
      std::stringstream ss;
      ss << "Count of records in PutRecords response differs from the number "
         << "sent: " << result.size() << "received, but "
         << prc->get_records().size() << " were sent.";
      LOG(error) << ss.str();
      for (auto& kr : prc->get_records()) {
        fail(kr, start, end, "Record Count Mismatch", ss.str());
      }
      return;
    }

    for (size_t i = 0; i < result.size(); i++) {
      auto& kr = prc->get_records()[i];
      auto& put_result = result[i];
      bool success = put_result.GetSequenceNumber().size() > 0;
      auto predicted_shard = kr->items().front()->predicted_shard();

      using aws::metrics::constants::Names;
      if (success) {
        metrics_putter
            (Names::KinesisRecordsPut, 1, predicted_shard)
            (Names::KinesisRecordsDataPut, kr->accurate_size(), predicted_shard)
            (Names::AllErrors, 0, predicted_shard);
      } else {
        metrics_putter
            (Names::KinesisRecordsPut, 0, predicted_shard)
            (Names::ErrorsByCode, 1, predicted_shard, put_result.GetErrorCode())
            (Names::AllErrors, 1, predicted_shard);
      }

      if (success) {
        for (auto& ur : kr->items()) {
          succeed_if_correct_shard(ur,
                                   start,
                                   end,
                                   put_result.GetShardId(),
                                   put_result.GetSequenceNumber());
        }
      } else {
        auto& err_code = put_result.GetErrorCode();
        auto& err_msg = put_result.GetErrorMessage();
        if (!(config_->fail_if_throttled() &&
              err_code == "ProvisionedThroughputExceededException")) {
          retry_not_expired(kr, start, end, err_code, err_msg);
        } else {
          fail(kr, start, end, err_code, err_msg);
        }
      }
    }
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

void Retrier::emit_metrics(const std::shared_ptr<PutRecordsContext>& prc) {
  using aws::metrics::constants::Names;
  detail::MetricsPutter metrics_putter(metrics_manager_, prc->get_stream());

  double num_urs = 0;
  for (auto& kr : prc->get_records()) {
    metrics_putter(Names::UserRecordsPerKinesisRecord,
                   kr->items().size(),
                   kr->items().front()->predicted_shard());
    num_urs += kr->items().size();
  }

  metrics_putter
      (Names::RequestTime, prc->duration_millis())
      (Names::KinesisRecordsPerPutRecordsRequest, prc->get_records().size())
      (Names::UserRecordsPerPutRecordsRequest, num_urs);

  if (!prc->get_outcome().IsSuccess()) {
    auto& code = prc->get_outcome().GetError().GetExceptionName();
    metrics_putter
        (Names::ErrorsByCode, 1, boost::none, code)
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
