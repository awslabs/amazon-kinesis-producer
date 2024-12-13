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

package software.amazon.kinesis.producer.sample;

import java.nio.ByteBuffer;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.Executors;
import java.util.concurrent.ThreadPoolExecutor;
import java.util.concurrent.atomic.AtomicLong;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import software.amazon.kinesis.producer.Attempt;
import software.amazon.kinesis.producer.KinesisProducerConfiguration;
import software.amazon.kinesis.producer.KinesisProducer;
import software.amazon.kinesis.producer.Metric;
import software.amazon.kinesis.producer.UserRecordFailedException;
import software.amazon.kinesis.producer.UserRecordResult;
import com.google.common.collect.Iterables;
import com.google.common.util.concurrent.FutureCallback;
import com.google.common.util.concurrent.Futures;
import com.google.common.util.concurrent.ListenableFuture;

/**
 * If you haven't looked at {@link SampleProducer}, do so first.
 * 
 * <p>
 * Here we'll look at using {@link KinesisProducer#getOutstandingRecordsCount()}
 * to control the rate at which we put data. The data we'll be using the is the
 * same as that described in {@link SampleProducer}
 * 
 * <p>
 * Instead of putting records at a constant rate, we're going to put as fast as
 * we can, up until a certain number of records are outstanding, then we'll wait
 * for some records to complete before putting more.
 * 
 * <p>
 * We'll also look at inspecting the metrics available from
 * {@link KinesisProducer#getMetrics(String, int)} to get sliding window
 * statistics.
 * 
 * @author chaodeng
 *
 */
public class MetricsAwareSampleProducer {
    private static final Logger log = LoggerFactory.getLogger(SampleProducer.class);
    
    public static void main(String[] args) throws InterruptedException, ExecutionException {
        final long totalRecordsToPut = 50000;
        final int dataSize = 64;
        final long outstandingLimit = 5000;
        
        final AtomicLong sequenceNumber = new AtomicLong(0);
        final AtomicLong completed = new AtomicLong(0);
        final String timetstamp = Long.toString(System.currentTimeMillis());
        
        KinesisProducerConfiguration config = new KinesisProducerConfiguration()
                .setRecordMaxBufferedTime(3000)
                .setMaxConnections(1)
                .setRequestTimeout(60000)
                .setRegion(SampleProducerConfig.REGION_DEFAULT);
        
        final KinesisProducer kinesisProducer = new KinesisProducer(config);
        
        // Result handler
        final FutureCallback<UserRecordResult> callback = new FutureCallback<UserRecordResult>() {
            @Override
            public void onFailure(Throwable t) {
                if (t instanceof UserRecordFailedException) {
                    Attempt last = Iterables.getLast(
                            ((UserRecordFailedException) t).getResult().getAttempts());
                    log.error(String.format(
                            "Record failed to put - %s : %s",
                            last.getErrorCode(), last.getErrorMessage()));
                }
                log.error("Exception during put", t);
                System.exit(1);
            }

            @Override
            public void onSuccess(UserRecordResult result) {
                completed.getAndIncrement();
            }
        };
        
        // Progress updates
        Thread progress = new Thread(new Runnable() {
            @Override
            public void run() {
                while (true) {
                    long put = sequenceNumber.get();
                    double putPercent = 100.0 * put / totalRecordsToPut;
                    long done = completed.get();
                    double donePercent = 100.0 * done / totalRecordsToPut;
                    log.info(String.format(
                            "Put %d of %d so far (%.2f %%), %d have completed (%.2f %%)",
                            put, totalRecordsToPut, putPercent, done, donePercent));
                    
                    if (done == totalRecordsToPut) {
                        break;
                    }

                    // Numerous metrics are available from the KPL locally, as
                    // well as uploaded to CloudWatch. See the metrics
                    // documentation for details.
                    //
                    // KinesisProducer provides methods to retrieve metrics for
                    // the current instance, with a customizable time window.
                    // This allows us to get sliding window statistics in real
                    // time for the current host.
                    //
                    // Here we're going to look at the number of user records
                    // put over a 5 seconds sliding window.
                    try {
                        for (Metric m : kinesisProducer.getMetrics("UserRecordsPut", 5)) {
                            // Metrics are emitted at different granularities, here
                            // we only look at the stream level metric, which has a
                            // single dimension of stream name.
                            if (m.getDimensions().size() == 1 && m.getSampleCount() > 0) {
                                log.info(String.format(
                                        "(Sliding 5 seconds) Avg put rate: %.2f per sec, success rate: %.2f, failure rate: %.2f, total attemped: %d",
                                        m.getSum() / 5,
                                        m.getSum() / m.getSampleCount() * 100,
                                        (m.getSampleCount() - m.getSum()) / m.getSampleCount() * 100,
                                        (long) m.getSampleCount()));
                            }
                        }
                    } catch (Exception e) {
                        log.error("Unexpected error getting metrics", e);
                        System.exit(1);
                    }
                    
                    try {
                        Thread.sleep(1000);
                    } catch (InterruptedException e) {}
                }
            }
        });
        progress.start();
        
        // Put records
        while (true) {
            // We're going to put as fast as we can until we've reached the max
            // number of records outstanding.
            if (sequenceNumber.get() < totalRecordsToPut) {
                if (kinesisProducer.getOutstandingRecordsCount() < outstandingLimit) {
                    ByteBuffer data = Utils.generateData(sequenceNumber.incrementAndGet(), dataSize);
                    ListenableFuture<UserRecordResult> f = kinesisProducer.addUserRecord(SampleProducerConfig.STREAM_NAME_DEFAULT,
                            timetstamp, data);
                    Futures.addCallback(f, callback, Executors.newSingleThreadExecutor());
                } else {
                    Thread.sleep(1);
                }
            } else {
                break;
            }
        }
        
        // Wait for remaining records to finish
        while (kinesisProducer.getOutstandingRecordsCount() > 0) {
            kinesisProducer.flush();
            Thread.sleep(100);
        }
        
        progress.join();
        
        for (Metric m : kinesisProducer.getMetrics("UserRecordsPerKinesisRecord")) {
            if (m.getDimensions().containsKey("ShardId")) {
                log.info(String.format(
                        "%.2f user records were aggregated into each Kinesis record on average for shard %s, for a total of %d Kinesis records.",
                        m.getMean(),
                        m.getDimensions().get("ShardId"),
                        (long) m.getSampleCount()));
            }
        }
        
        kinesisProducer.destroy();
        log.info("Finished.");
    }
}
