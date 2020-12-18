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

package com.amazonaws.services.kinesis.producer.sample;

import java.nio.ByteBuffer;
import java.util.concurrent.Executors;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicLong;

import com.amazonaws.services.kinesis.producer.UnexpectedMessageException;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.amazonaws.auth.DefaultAWSCredentialsProviderChain;
import com.amazonaws.services.kinesis.producer.Attempt;
import com.amazonaws.services.kinesis.producer.KinesisProducerConfiguration;
import com.amazonaws.services.kinesis.producer.KinesisProducer;
import com.amazonaws.services.kinesis.producer.UserRecordFailedException;
import com.amazonaws.services.kinesis.producer.UserRecordResult;
import com.google.common.util.concurrent.FutureCallback;
import com.google.common.util.concurrent.Futures;
import com.google.common.util.concurrent.ListenableFuture;

/**
 * The Kinesis Producer Library (KPL) excels at handling large numbers of small
 * logical records by combining multiple logical records into a single Kinesis
 * record.
 * 
 * <p>
 * In this sample we'll be putting a monotonically increasing sequence number in
 * each logical record, and then padding the record to 128 bytes long. The
 * consumer will then check that all records are received correctly by verifying
 * that there are no gaps in the sequence numbers.
 * 
 * <p>
 * We will distribute the records evenly across all shards by using a random
 * explicit hash key.
 * 
 * <p>
 * To prevent the consumer from being confused by data from multiple runs of the
 * producer, each record also carries the time at which the producer started.
 * The consumer will reset its state whenever it detects a new, larger
 * timestamp. We will place the timestamp in the partition key. This does not
 * affect the random distribution of records across shards since we've set an
 * explicit hash key.
 * 
 * @see SampleConsumer
 * @author chaodeng
 *
 */
public class SampleProducer {
    private static final Logger log = LoggerFactory.getLogger(SampleProducer.class);
    
    private static final ScheduledExecutorService EXECUTOR = Executors.newScheduledThreadPool(1);
    
    /**
     * Timestamp we'll attach to every record
     */
    private static final String TIMESTAMP = Long.toString(System.currentTimeMillis());
    
    /** The main method.
     *  @param args  The command line args for the Sample Producer.
     *  @see com.amazonaws.services.kinesis.producer.sample.SampleProducerConfig for positional arg ordering.
     */
    public static void main(String[] args) throws Exception {
        final SampleProducerConfig config = new SampleProducerConfig(args);

        log.info(String.format("Stream name: %s Region: %s secondsToRun %d",config.getStreamName(), config.getRegion(),
                config.getSecondsToRun()));
        log.info(String.format("Will attempt to run the KPL at %f MB/s...",(config.getDataSize() * config
                .getRecordsPerSecond())/(1000000.0)));

        final KinesisProducer producer = new KinesisProducer(config.transformToKinesisProducerConfiguration());
        
        // The monotonically increasing sequence number we will put in the data of each record
        final AtomicLong sequenceNumber = new AtomicLong(0);
        
        // The number of records that have finished (either successfully put, or failed)
        final AtomicLong completed = new AtomicLong(0);
        
        // KinesisProducer.addUserRecord is asynchronous. A callback can be used to receive the results.
        final FutureCallback<UserRecordResult> callback = new FutureCallback<UserRecordResult>() {
            @Override
            public void onFailure(Throwable t) {
                // If we see any failures, we will log them.
                if (t instanceof UserRecordFailedException) {
                    int attempts = ((UserRecordFailedException) t).getResult().getAttempts().size()-1;
                    Attempt last = ((UserRecordFailedException) t).getResult().getAttempts().get(attempts);
                    if(attempts > 1) {
                        Attempt previous = ((UserRecordFailedException) t).getResult().getAttempts().get(attempts - 1);
                        log.error(String.format(
                                "Record failed to put - %s : %s. Previous failure - %s : %s",
                                last.getErrorCode(), last.getErrorMessage(), previous.getErrorCode(), previous.getErrorMessage()));
                    }else{
                        log.error(String.format(
                                "Record failed to put - %s : %s.",
                                last.getErrorCode(), last.getErrorMessage()));
                    }

                } else if (t instanceof UnexpectedMessageException) {
                    log.error("Record failed to put due to unexpected message received from native layer",
                            t);
                }
                log.error("Exception during put", t);
            }

            @Override
            public void onSuccess(UserRecordResult result) {
                completed.getAndIncrement();
            }
        };
        
        final ExecutorService callbackThreadPool = Executors.newCachedThreadPool();

        // The lines within run() are the essence of the KPL API.
        final Runnable putOneRecord = new Runnable() {
            @Override
            public void run() {
                ByteBuffer data = Utils.generateData(sequenceNumber.get(), config.getDataSize());
                // TIMESTAMP is our partition key
                ListenableFuture<UserRecordResult> f =
                        producer.addUserRecord(config.getStreamName(), TIMESTAMP, Utils.randomExplicitHashKey(), data);
                Futures.addCallback(f, callback, callbackThreadPool);
            }
        };
        
        // This gives us progress updates
        EXECUTOR.scheduleAtFixedRate(new Runnable() {
            @Override
            public void run() {
                long put = sequenceNumber.get();
                long total = config.getRecordsPerSecond() * config.getSecondsToRun();
                double putPercent = 100.0 * put / total;
                long done = completed.get();
                double donePercent = 100.0 * done / total;
                log.info(String.format(
                        "Put %d of %d so far (%.2f %%), %d have completed (%.2f %%)",
                        put, total, putPercent, done, donePercent));
                log.info(String.format("Oldest future as of now in millis is %s", producer.getOldestRecordTimeInMillis
                        ()));
            }
        }, 1, 1, TimeUnit.SECONDS);
        
        // Kick off the puts
        log.info(String.format(
                "Starting puts... will run for %d seconds at %d records per second", config.getSecondsToRun(),
                config.getRecordsPerSecond()));
        executeAtTargetRate(EXECUTOR, putOneRecord, sequenceNumber, config.getSecondsToRun(),
                config.getRecordsPerSecond());
        
        // Wait for puts to finish. After this statement returns, we have
        // finished all calls to putRecord, but the records may still be
        // in-flight. We will additionally wait for all records to actually
        // finish later.
        EXECUTOR.awaitTermination(config.getSecondsToRun() + 1, TimeUnit.SECONDS);
        
        // If you need to shutdown your application, call flushSync() first to
        // send any buffered records. This method will block until all records
        // have finished (either success or fail). There are also asynchronous
        // flush methods available.
        //
        // Records are also automatically flushed by the KPL after a while based
        // on the time limit set with Configuration.setRecordMaxBufferedTime()
        log.info("Waiting for remaining puts to finish...");
        producer.flushSync();
        log.info("All records complete.");
        
        // This kills the child process and shuts down the threads managing it.
        producer.destroy();
        log.info("Finished.");
    }

    /**
     * Executes a function N times per second for M seconds with a
     * ScheduledExecutorService. The executor is shutdown at the end. This is
     * more precise than simply using scheduleAtFixedRate.
     * 
     * @param exec
     *            Executor
     * @param task
     *            Task to perform
     * @param counter
     *            Counter used to track how many times the task has been
     *            executed
     * @param durationSeconds
     *            How many seconds to run for
     * @param ratePerSecond
     *            How many times to execute task per second
     */
    private static void executeAtTargetRate(
            final ScheduledExecutorService exec,
            final Runnable task,
            final AtomicLong counter,
            final int durationSeconds,
            final int ratePerSecond) {
        exec.scheduleWithFixedDelay(new Runnable() {
            final long startTime = System.nanoTime();

            @Override
            public void run() {
                double secondsRun = (System.nanoTime() - startTime) / 1e9;
                double targetCount = Math.min(durationSeconds, secondsRun) * ratePerSecond;
                
                while (counter.get() < targetCount) {
                    counter.getAndIncrement();
                    try {
                        task.run();
                    } catch (Exception e) {
                        log.error("Error running task", e);
                        System.exit(1);
                    }
                }
                
                if (secondsRun >= durationSeconds) {
                    exec.shutdown();
                }
            }
        }, 0, 1, TimeUnit.MILLISECONDS);
    }
    

}
