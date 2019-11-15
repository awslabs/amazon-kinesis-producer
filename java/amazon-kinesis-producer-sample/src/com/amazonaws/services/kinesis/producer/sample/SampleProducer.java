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
import java.util.List;
import java.util.concurrent.Executors;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicLong;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.amazonaws.auth.DefaultAWSCredentialsProviderChain;
import com.amazonaws.services.kinesis.producer.Attempt;
import com.amazonaws.services.kinesis.producer.KinesisProducerConfiguration;
import com.amazonaws.services.kinesis.producer.KinesisProducer;
import com.amazonaws.services.kinesis.producer.UserRecordFailedException;
import com.amazonaws.services.kinesis.producer.UserRecordResult;
import com.google.common.collect.Iterables;
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
    
    /**
     * Change these to try larger or smaller records.
     */
    private static final int DATA_SIZE_DEFAULT = 128;
    
    /**
     * Put records for this number of seconds before exiting.
     */
    private static final int SECONDS_TO_RUN_DEFAULT = 5;
    
    /**
     * Put this number of records per second.
     * 
     * Because multiple logical records are combined into each Kinesis record,
     * even a single shard can handle several thousand records per second, even
     * though there is a limit of 1000 Kinesis records per shard per second.
     * 
     * If a shard gets throttled, the KPL will continue to retry records until
     * either they succeed or reach a TTL set in the KPL's configuration, at
     * which point the KPL will return failures for those records.
     * 
     * @see {@link KinesisProducerConfiguration#setRecordTtl(long)}
     */
    private static final int RECORDS_PER_SECOND_DEFAULT = 2000;
    
    /**
     * Change this to your stream name.
     */
    public static final String STREAM_NAME_DEFAULT = "test";
    
    /**
     * Change this to the region you are using.
     */
    public static final String REGION_DEFAULT = "us-west-1";

    /**
     * Here'll walk through some of the config options and create an instance of
     * KinesisProducer, which will be used to put records.
     *
     * @param region The region of the Kinesis stream being used.
     *
     * @param connections The max number of connections to use within the KPL to service AWS Requests
     * @param requestTimeout The configured request timeout for requests to Kinesis
     * @param bufferTime The configured buffer time for the KPL
     * @param threadingModel The configured ThreadingModel to use for the KPL. Either "POOLED" or "PER_REQUEST"
     * @param numberOfThreads When POOLED threading model is used, the number of native threads to run in the
     *                       Daemon to serve Kinesis requests.
     * @return KinesisProducer instance used to put records.
     */
    public static KinesisProducer getKinesisProducer(final String region, int connections, int requestTimeout,
            int bufferTime, String threadingModel, int numberOfThreads) {
        // There are many configurable parameters in the KPL. See the javadocs
        // on each each set method for details.
        KinesisProducerConfiguration config = new KinesisProducerConfiguration();
        
        // You can also load config from file. A sample properties file is
        // included in the project folder.
        // KinesisProducerConfiguration config =
        //     KinesisProducerConfiguration.fromPropertiesFile("default_config.properties");
        
        // If you're running in EC2 and want to use the same Kinesis region as
        // the one your instance is in, you can simply leave out the region
        // configuration; the KPL will retrieve it from EC2 metadata.
        config.setRegion(region);
        
        // You can pass credentials programmatically through the configuration,
        // similar to the AWS SDK. DefaultAWSCredentialsProviderChain is used
        // by default, so this configuration can be omitted if that is all
        // that is needed.
        config.setCredentialsProvider(new DefaultAWSCredentialsProviderChain());
        
        // The maxConnections parameter can be used to control the degree of
        // parallelism when making HTTP requests. We're going to use only 1 here
        // since our throughput is fairly low. Using a high number will cause a
        // bunch of broken pipe errors to show up in the logs. This is due to
        // idle connections being closed by the server. Setting this value too
        // large may also cause request timeouts if you do not have enough
        // bandwidth.
        config.setMaxConnections(connections);
        
        // Set a more generous timeout in case we're on a slow connection.
        config.setRequestTimeout(requestTimeout);
        
        // RecordMaxBufferedTime controls how long records are allowed to wait
        // in the KPL's buffers before being sent. Larger values increase
        // aggregation and reduces the number of Kinesis records put, which can
        // be helpful if you're getting throttled because of the records per
        // second limit on a shard. The default value is set very low to
        // minimize propagation delay, so we'll increase it here to get more
        // aggregation.
        config.setRecordMaxBufferedTime(bufferTime);

        // Configures the threading model for the KPL. By default the KPL
        // launches with a PER_REQUEST threading model with a threadpool size
        // of 0. At larger stream sizes it becomes more beneficial to change
        // to a pooled threading model.
        config.setThreadingModel(threadingModel);
        config.setThreadPoolSize(numberOfThreads);

        // If you have built the native binary yourself, you can point the Java
        // wrapper to it with the NativeExecutable option. If you want to pass
        // environment variables to the executable, you can either use a wrapper
        // shell script, or set them for the Java process, which will then pass
        // them on to the child process.
        // config.setNativeExecutable("my_directory/kinesis_producer");
        
        // If you end up using the default configuration (a Configuration instance
        // without any calls to set*), you can just leave the config argument
        // out.
        //
        // Note that if you do pass a Configuration instance, mutating that
        // instance after initializing KinesisProducer has no effect. We do not
        // support dynamic re-configuration at the moment.
        KinesisProducer producer = new KinesisProducer(config);
        
        return producer;
    }

    public static String getArgIfPresent(final String[] args, final int index, final String defaultValue) {
        return args.length > index ? args[index] : defaultValue;
    }

    /** The main method.
     *  @param args  The command line args for the Sample Producer. It takes 3 optional position parameters:
     *  1. The stream name to use (test is default)
     *  2. The region name to use (us-west-1 in default)
     *  3. The duration of the test in seconds, 5 is the default.
     *  4. The number of records per second to send, 2000 is the default.
     *  5. The payload size of each record being sent in bytes, 128 is the default.
     *  6. The max number of connections to configure the KPL with, 1 is the default.
     *  7. The requestTimeout in milliseconds to configure the KPL with, 60000 is the default.
     *  8. The bufferTime in milliseconds to configure the KPL with, 2000 is the default.
     *  9. The threading model to configure the KPL with, PER_REQUEST is the default.
     *  10. The threadPoolSize to configure the KPL with, 0 is the default.
     */
    public static void main(String[] args) throws Exception {
        int argIndex = 0;
        final String streamName = getArgIfPresent(args, argIndex++, STREAM_NAME_DEFAULT);
        final String region = getArgIfPresent(args, argIndex++, REGION_DEFAULT);
        final int secondsToRun = Integer.parseInt(getArgIfPresent(args, argIndex++, String.valueOf(SECONDS_TO_RUN_DEFAULT)));
        final int records_per_second = Integer.parseInt(getArgIfPresent(args, argIndex++, String.valueOf(RECORDS_PER_SECOND_DEFAULT)));
        final int data_size = Integer.parseInt(getArgIfPresent(args, argIndex++, String.valueOf(DATA_SIZE_DEFAULT)));
        final int connections = Integer.parseInt(getArgIfPresent(args, argIndex++, String.valueOf(1)));
        final int requestTimeout = Integer.parseInt(getArgIfPresent(args, argIndex++, String.valueOf(60000)));
        final int bufferTime = Integer.parseInt(getArgIfPresent(args, argIndex++, String.valueOf(2000)));
        final String threadingModel = getArgIfPresent(args, argIndex++, KinesisProducerConfiguration.ThreadingModel.PER_REQUEST.name());
        final int threadPoolSize = Integer.parseInt(getArgIfPresent(args, argIndex++, String.valueOf(0)));
        boolean errorsFound=false;
        if (secondsToRun <= 0) {
            errorsFound=true;
            log.error("SecondsToRun should be a positive integer");
        }

        if (records_per_second <= 0) {
            errorsFound=true;
            log.error("RecordsPerSecond should be a positive integer");
        }

        if (data_size <= 0) {
            errorsFound=true;
            log.error("DataSize should be a positive integer");
        }

        if (connections <= 0) {
            errorsFound=true;
            log.error("Connections should be a positive integer");
        }

        if (requestTimeout <= 0) {
            errorsFound=true;
            log.error("RequestTimeout should be a positive integer");
        }

        if (bufferTime <= 0) {
            errorsFound=true;
            log.error("BufferTime should be a positive integer");
        }

        if (threadPoolSize < 0) {
            errorsFound=true;
            log.error("ThreadPoolSize should be a positive integer, or 0");
        }

        if(!threadingModel.equals(KinesisProducerConfiguration.ThreadingModel.PER_REQUEST.name()) &&
                !threadingModel.equals(KinesisProducerConfiguration.ThreadingModel.POOLED.name())){
            errorsFound=true;
            log.error("Threading model needs to be one of [PER_REQUEST | POOLED]");
        }

        if (threadPoolSize < 0) {
            errorsFound=true;
            log.error("ThreadPoolSize should be a positive integer, or 0");
        }

        if(errorsFound){
            System.exit(1);
        }

        log.info(String.format("Stream name: %s Region: %s secondsToRun %d",streamName, region, secondsToRun));
        log.info(String.format("Will attempt to run the KPL at %f MB/s...",(data_size*records_per_second)/(1000000.0)));

        final KinesisProducer producer = getKinesisProducer(region, connections, requestTimeout, bufferTime,
                threadingModel,threadPoolSize);
        
        // The monotonically increasing sequence number we will put in the data of each record
        final AtomicLong sequenceNumber = new AtomicLong(0);
        
        // The number of records that have finished (either successfully put, or failed)
        final AtomicLong completed = new AtomicLong(0);
        
        // KinesisProducer.addUserRecord is asynchronous. A callback can be used to receive the results.
        final FutureCallback<UserRecordResult> callback = new FutureCallback<UserRecordResult>() {
            @Override
            public void onFailure(Throwable t) {
                // If we see any failures, we will log them.
                int attempts = ((UserRecordFailedException) t).getResult().getAttempts().size()-1;
                if (t instanceof UserRecordFailedException) {
                    Attempt previous = ((UserRecordFailedException) t).getResult().getAttempts().get(attempts-1);
                    Attempt last = ((UserRecordFailedException) t).getResult().getAttempts().get(attempts);
                    log.error(String.format(
                            "Record failed to put - %s : %s. Previous failure - %s : %s",
                            last.getErrorCode(), last.getErrorMessage(), previous.getErrorCode(), previous.getErrorMessage()));
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
                ByteBuffer data = Utils.generateData(sequenceNumber.get(), data_size);
                // TIMESTAMP is our partition key
                ListenableFuture<UserRecordResult> f =
                        producer.addUserRecord(streamName, TIMESTAMP, Utils.randomExplicitHashKey(), data);
                Futures.addCallback(f, callback, callbackThreadPool);
            }
        };
        
        // This gives us progress updates
        EXECUTOR.scheduleAtFixedRate(new Runnable() {
            @Override
            public void run() {
                long put = sequenceNumber.get();
                long total = records_per_second * secondsToRun;
                double putPercent = 100.0 * put / total;
                long done = completed.get();
                double donePercent = 100.0 * done / total;
                log.info(String.format(
                        "Put %d of %d so far (%.2f %%), %d have completed (%.2f %%)",
                        put, total, putPercent, done, donePercent));
            }
        }, 1, 1, TimeUnit.SECONDS);
        
        // Kick off the puts
        log.info(String.format(
                "Starting puts... will run for %d seconds at %d records per second",
                secondsToRun, records_per_second));
        executeAtTargetRate(EXECUTOR, putOneRecord, sequenceNumber, secondsToRun, records_per_second);
        
        // Wait for puts to finish. After this statement returns, we have
        // finished all calls to putRecord, but the records may still be
        // in-flight. We will additionally wait for all records to actually
        // finish later.
        EXECUTOR.awaitTermination(secondsToRun + 1, TimeUnit.SECONDS);
        
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
