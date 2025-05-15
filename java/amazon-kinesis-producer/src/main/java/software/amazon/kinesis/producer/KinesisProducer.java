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

package software.amazon.kinesis.producer;

import software.amazon.kinesis.producer.protobuf.Messages;
import software.amazon.kinesis.producer.protobuf.Messages.Flush;
import software.amazon.kinesis.producer.protobuf.Messages.Message;
import software.amazon.kinesis.producer.protobuf.Messages.MetricsRequest;
import software.amazon.kinesis.producer.protobuf.Messages.MetricsResponse;
import software.amazon.kinesis.producer.protobuf.Messages.PutRecord;
import com.amazonaws.services.schemaregistry.common.Schema;
import com.amazonaws.services.schemaregistry.serializers.GlueSchemaRegistrySerializer;
import com.google.common.collect.ImmutableMap;
import com.google.common.util.concurrent.ListenableFuture;
import com.google.common.util.concurrent.SettableFuture;
import com.google.common.util.concurrent.ThreadFactoryBuilder;
import com.google.protobuf.ByteString;
import lombok.AllArgsConstructor;
import lombok.NonNull;
import lombok.Value;
import org.apache.commons.lang3.StringUtils;
import org.apache.commons.lang3.SystemUtils;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.math.BigInteger;
import java.nio.ByteBuffer;
import java.nio.file.Paths;
import java.time.Instant;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.FutureTask;
import java.util.concurrent.LinkedBlockingQueue;
import java.util.concurrent.PriorityBlockingQueue;
import java.util.concurrent.RejectedExecutionHandler;
import java.util.concurrent.ScheduledThreadPoolExecutor;
import java.util.concurrent.ThreadPoolExecutor;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicLong;
/**
 * An interface to the native KPL daemon. This class handles the creation,
 * destruction and use of the child process.
 * 
 * <p>
 * <b>Use a single instance within the application whenever possible:</b>
 * <p>
 * <ul>
 * <li>One child process is spawned per instance of KinesisProducer. Additional
 * instances introduce overhead and reduce aggregation efficiency.</li>
 * <li>All streams within a region that can be accessed with the same
 * credentials can share the same KinesisProducer instance.</li>
 * <li>The {@link #addUserRecord} methods are thread safe, and be called
 * concurrently.</li>
 * <li>Therefore, unless you need to put to multiple regions, or need to use
 * different credentials for different streams, you should avoid creating
 * multiple instances of KinesisProducer.</li>
 * </ul>
 * <p>
 * 
 * @author chaodeng
 *
 */
public class KinesisProducer implements IKinesisProducer {
    private static final Logger log = LoggerFactory.getLogger(KinesisProducer.class);
    private static final BigInteger UINT_128_MAX = new BigInteger(StringUtils.repeat("FF", 16), 16);
    private static final Object EXTRACT_BIN_MUTEX = new Object();
    
    private static final AtomicInteger CALLBACK_COMPLETION_POOL_NUMBER = new AtomicInteger(0);
    private static final int CALLBACK_COMPLETION_POOL_SIZE = Runtime.getRuntime().availableProcessors() * 4;
    
    private final KinesisProducerConfiguration config;
    private final Map<String, String> env;
    private final AtomicLong messageNumber = new AtomicLong(1);
    private final AtomicLong totalFutureTimeouts = new AtomicLong(0);
    private final GlueSchemaRegistrySerializerInstance glueSchemaRegistrySerializerInstance = new GlueSchemaRegistrySerializerInstance();
    private final Map<Long, SettableFutureTracker> futures = new ConcurrentHashMap<>();
    private final PriorityBlockingQueue<SettableFutureTracker> oldestFutureTrackerHeap = new PriorityBlockingQueue
            (10, new SettableFutureTrackerComparator());
    private final ScheduledThreadPoolExecutor futureTimeoutExecutor = new ScheduledThreadPoolExecutor(1,
            new ThreadFactoryBuilder()
            .setDaemon(true)
            .setNameFormat("kpl-timeout-future-" + CALLBACK_COMPLETION_POOL_NUMBER.getAndIncrement() + "-thread-%d")
            .build());


    private static class SettableFutureTrackerComparator implements Comparator<SettableFutureTracker>
    {
        public int compare(SettableFutureTracker x, SettableFutureTracker y)
        {
            return Long.compare(x.getTimestamp().toEpochMilli(), y.getTimestamp().toEpochMilli());
        }
    }

    @Value
    private static class SettableFutureTracker {
        @NonNull
        private SettableFuture<?> future;
        @NonNull
        private Instant timestamp;
        @NonNull
        private Optional<FutureTask> timeoutTask;

        private void cancelTimeoutTaskIfPresent() {
            timeoutTask.ifPresent(t -> t.cancel(false));
        }
    }

    // Creating a fixed thread pool as we use unbounded queue.
    private final ExecutorService callbackCompletionExecutor = new ThreadPoolExecutor(
            CALLBACK_COMPLETION_POOL_SIZE,
            CALLBACK_COMPLETION_POOL_SIZE,
            5,
            TimeUnit.MINUTES,
            new LinkedBlockingQueue<Runnable>(),
            new ThreadFactoryBuilder()
                    .setDaemon(true)
                    .setNameFormat("kpl-callback-pool-" + CALLBACK_COMPLETION_POOL_NUMBER.getAndIncrement() + "-thread-%d")
                    .build(),
            new RejectedExecutionHandler() {
                /**
                 * Execute the runnable inline if we can't submit it to the
                 * executor. This shouldn't happen since we're using a linked
                 * queue which doesn't have a bound; but it's here just in case.
                 */
                @Override
                public void rejectedExecution(Runnable r, ThreadPoolExecutor executor) {
                    r.run();
                }
            });


    private String pathToExecutable;
    private String pathToLibDir;
    private String pathToTmpDir;
    
    private volatile Daemon child;
    private volatile long lastChild = System.nanoTime();
    private volatile boolean destroyed = false;
    private ProcessFailureBehavior processFailureBehavior = ProcessFailureBehavior.AutoRestart;

    private class MessageHandler implements Daemon.MessageHandler {
        @Override
        public void onMessage(final Message m) {
            callbackCompletionExecutor.execute(new Runnable() {
                @Override
                public void run() {
                    if (m.hasPutRecordResult()) {
                        onPutRecordResult(m);
                    } else if (m.hasMetricsResponse()) {
                        onMetricsResponse(m);
                    } else {
                        // clear the future here as well since the native core has exhausted its retries.
                        SettableFutureTracker futureTracker = getFuture(m);
                        SettableFuture<?> f = futureTracker.getFuture();
                        f.setException(new UnexpectedMessageException("Unexpected message type from child process"));
                        log.error(String.format("Unexpected message type with case %s from child process with message"
                                        + " id %s. Removing the submitted future from processing queue.",
                                m.getActualMessageCase(), m.getSourceId()));

                    }
                }
            });
        }

        @Override
        public void onError(final Throwable t) {
            // Don't log error if the user called destroy
            if (!destroyed) {
                log.error("Error in child process", t);
            }

            // Fail all outstanding futures
            for (final Map.Entry<Long, SettableFutureTracker> entry : futures.entrySet()) {
                callbackCompletionExecutor.execute(new Runnable() {
                    @Override
                    public void run() {
                        entry.getValue().getFuture().setException(t);
                    }
                });
            }
            futures.clear();
            oldestFutureTrackerHeap.clear();

            if (processFailureBehavior == ProcessFailureBehavior.AutoRestart && !destroyed) {
                log.info("Restarting native producer process.");
                child = new Daemon(pathToExecutable, new MessageHandler(), pathToTmpDir, config, env);
            } else {
                // Only restart child if it's not an irrecoverable error, and if
                // there has been some time (3 seconds) between the last child
                // creation. If the child process crashes almost immediately, we're
                // going to abort to avoid going into a loop.
                if (!(t instanceof IrrecoverableError) && System.nanoTime() - lastChild > 3e9) {
                    lastChild = System.nanoTime();
                    child = new Daemon(pathToExecutable, new MessageHandler(), pathToTmpDir, config, env);
                }
            }
        }
  
        /**
         * Matches up the incoming PutRecordResult to an outstanding future, and
         * completes that future with the appropriate data.
         * 
         * <p>
         * We adapt the protobuf PutRecordResult to another simpler
         * PutRecordResult class to hide the protobuf stuff from the user.
         * 
         * @param msg
         */
        private void onPutRecordResult(Message msg) {
            SettableFutureTracker futureTracker = getFuture(msg);
            SettableFuture<UserRecordResult> f = (SettableFuture<UserRecordResult>) futureTracker.getFuture();
            UserRecordResult result = UserRecordResult.fromProtobufMessage(msg.getPutRecordResult());
            if (result.isSuccessful()) {
                f.set(result);
            } else {
                f.setException(new UserRecordFailedException(result));
            }
        }
        
        private void onMetricsResponse(Message msg) {
            SettableFutureTracker futureTracker = getFuture(msg);
            SettableFuture<List<Metric>> f = (SettableFuture<List<Metric>>) futureTracker.getFuture();
            List<Metric> userMetrics = new ArrayList<>();
            MetricsResponse res = msg.getMetricsResponse();
            for (Messages.Metric metric : res.getMetricsList()) {
                userMetrics.add(new Metric(metric));
            }
            
            f.set(userMetrics);
        }
        
        private SettableFutureTracker getFuture(Message msg) {
            long id = msg.getSourceId();
            SettableFutureTracker futureTracker = getFutureTracker(id);
            // Cancel the future timeout task if present to relieve memory from the ScheduledThreadPoolExecutor
            futureTracker.cancelTimeoutTaskIfPresent();
            return  futureTracker;
        }
    }

    private SettableFutureTracker getFutureTracker(long id) {
        @SuppressWarnings("unchecked")
        SettableFutureTracker futureTracker = futures.remove(id);
        if (futureTracker == null) {
            String message = "Future for message id "+ id +" not found as potentially it was a duplicate message "
                    + "or was timed out in Java layer.";
            log.error(message);
            throw new RuntimeException(message);
        }
        oldestFutureTrackerHeap.remove(futureTracker);
        return futureTracker;
    }
    
    /**
     * Start up a KinesisProducer instance.
     * 
     * <p>
     * Since this creates a child process, it is fairly expensive. Avoid
     * creating more than one per application, unless putting to multiple
     * regions at the same time. All streams in the same region can share the
     * same instance.
     * 
     * <p>
     * All methods in KinesisProducer are thread-safe.
     * 
     * @param config
     *            Configuration for the KinesisProducer. See the docs for that
     *            class for details.
     * 
     * @see KinesisProducerConfiguration
     */
    public KinesisProducer(KinesisProducerConfiguration config) {
        this.config = config;
        String caPath = config.getCaCertPath();
        String caFile = config.getCaCertFile();
        String caDirectory = extractBinaries();
        // Override the CA cert path if provided by the user config
        if(!StringUtils.isEmpty(caPath)) {
            log.info("Overrding the ca cert path to use as provided in the kpl config to be " + caPath);
            caDirectory = caPath;
        }

        env = new ImmutableMap.Builder<String, String>()
                .put("LD_LIBRARY_PATH", pathToLibDir)
                .put("DYLD_LIBRARY_PATH", pathToLibDir)
                .put("CA_DIR", caDirectory)
                .put("CA_FILE", caFile)
                .build();

        // We set the policy for the executor service to remove queued tasks if they are cancelled to relieve
        // unwanted cpu load.
        futureTimeoutExecutor.setRemoveOnCancelPolicy(true);

        child = new Daemon(pathToExecutable, new MessageHandler(), pathToTmpDir, config, env);
    }
    
    /**
     * Start up a KinesisProducer instance.
     * 
     * <p>
     * Since this creates a child process, it is fairly expensive. Avoid
     * creating more than one per application, unless putting to multiple
     * regions at the same time. All streams in the same region can share the
     * same instance.
     * 
     * <p>
     * The KPL will use a set of default configurations. You can set custom
     * configuration using the constructor that takes a {@link KinesisProducerConfiguration}
     * object.
     * 
     * <p>
     * All methods in KinesisProducer are thread-safe.
     */
    public KinesisProducer() {
        this(new KinesisProducerConfiguration());
    }
    
    /**
     * Connect to a running daemon. Does not start a child process. Used for
     * testing.
     * 
     * @param inPipe
     * @param outPipe
     */
    protected KinesisProducer(File inPipe, File outPipe) {
        this.config = null;
        this.env = null;
        child = new Daemon(inPipe, outPipe, new MessageHandler());
    }
    
    /**
     * Put a record asynchronously. A {@link ListenableFuture} is returned that
     * can be used to retrieve the result, either by polling or by registering a
     * callback.
     * 
     * <p>
     * The return value can be disregarded if you do not wish to process the
     * result. Under the covers, the KPL will automatically re-attempt puts in
     * case of transient errors (including throttling). A failed result is
     * generally returned only if an irrecoverable error is detected (e.g.
     * trying to put to a stream that doesn't exist), or if the record expires.
     *
     * <p>
     * <b>Thread safe.</b>
     * 
     * <p>
     * To add a listener to the future:
     * <p>
     * <code>
     * ListenableFuture&lt;PutRecordResult&gt; f = myKinesisProducer.addUserRecord(...);
     * com.google.common.util.concurrent.Futures.addCallback(f, callback, executor);
     * </code>
     * <p>
     * where <code>callback</code> is an instance of
     * {@link com.google.common.util.concurrent.FutureCallback} and
     * <code>executor</code> is an instance of
     * {@link java.util.concurrent.Executor}.
     * <p>
     * <b>Important:</b>
     * <p>
     * If long-running tasks are performed in the callbacks, it is recommended
     * that a custom executor be provided when registering callbacks to ensure
     * that there are enough threads to achieve the desired level of
     * parallelism. By default, the KPL will use an internal thread pool to
     * execute callbacks, but this pool may not have a sufficient number of
     * threads if a large number is desired.
     * <p>
     * Another option would be to hand the result off to a different component
     * for processing and keep the callback routine fast.
     * 
     * @param stream
     *            Stream to put to.
     * @param partitionKey
     *            Partition key. Length must be at least one, and at most 256
     *            (inclusive).
     * @param data
     *            Binary data of the record. Maximum size 1MiB.
     * @return A future for the result of the put.
     * @throws IllegalArgumentException
     *             if input does not meet stated constraints
     * @throws DaemonException
     *             if the child process is dead
     * @see ListenableFuture
     * @see UserRecordResult
     * @see KinesisProducerConfiguration#setRecordTtl(long)
     * @see UserRecordFailedException
     */
    @Override
    public ListenableFuture<UserRecordResult> addUserRecord(String stream, String partitionKey, ByteBuffer data) {
        return addUserRecord(stream, partitionKey, null, data);
    }

    /**
     * Put a record asynchronously. A {@link ListenableFuture} is returned that
     * can be used to retrieve the result, either by polling or by registering a
     * callback.
     *
     * <p>
     * The return value can be disregarded if you do not wish to process the
     * result. Under the covers, the KPL will automatically re-attempt puts in
     * case of transient errors (including throttling). A failed result is
     * generally returned only if an irrecoverable error is detected (e.g.
     * trying to put to a stream that doesn't exist), or if the record expires.
     *
     * <p>
     * <b>Thread safe.</b>
     *
     * <p>
     * To add a listener to the future:
     * <p>
     * <code>
     * ListenableFuture&lt;PutRecordResult&gt; f = myKinesisProducer.addUserRecord(...);
     * com.google.common.util.concurrent.Futures.addCallback(f, callback, executor);
     * </code>
     * <p>
     * where <code>callback</code> is an instance of
     * {@link com.google.common.util.concurrent.FutureCallback} and
     * <code>executor</code> is an instance of
     * {@link java.util.concurrent.Executor}.
     * <p>
     * <b>Important:</b>
     * <p>
     * If long-running tasks are performed in the callbacks, it is recommended
     * that a custom executor be provided when registering callbacks to ensure
     * that there are enough threads to achieve the desired level of
     * parallelism. By default, the KPL will use an internal thread pool to
     * execute callbacks, but this pool may not have a sufficient number of
     * threads if a large number is desired.
     * <p>
     * Another option would be to hand the result off to a different component
     * for processing and keep the callback routine fast.
     *
     * @param userRecord
     *            All data necessary to write to the stream.
     * @return A future for the result of the put.
     * @throws IllegalArgumentException
     *             if input does not meet stated constraints
     * @throws DaemonException
     *             if the child process is dead
     * @see ListenableFuture
     * @see UserRecordResult
     * @see KinesisProducerConfiguration#setRecordTtl(long)
     * @see UserRecordFailedException
     */
    @Override
    public ListenableFuture<UserRecordResult> addUserRecord(UserRecord userRecord) {
        return addUserRecord(userRecord.getStreamName(), userRecord.getPartitionKey(), userRecord.getExplicitHashKey(), userRecord.getData(), userRecord.getSchema());
    }

    /**
     * Put a record asynchronously. A {@link ListenableFuture} is returned that
     * can be used to retrieve the result, either by polling or by registering a
     * callback.
     * 
     * <p>
     * The return value can be disregarded if you do not wish to process the
     * result. Under the covers, the KPL will automatically reattempt puts in
     * case of transient errors (including throttling). A failed result is
     * generally returned only if an irrecoverable error is detected (e.g.
     * trying to put to a stream that doesn't exist), or if the record expires.
     *
     * <p>
     * <b>Thread safe.</b>
     * 
     * <p>
     * To add a listener to the future:
     * <p>
     * <code>
     * ListenableFuture&lt;PutRecordResult&gt; f = myKinesisProducer.addUserRecord(...);
     * com.google.common.util.concurrent.Futures.addCallback(f, callback, executor);
     * </code>
     * <p>
     * where <code>callback</code> is an instance of
     * {@link com.google.common.util.concurrent.FutureCallback} and
     * <code>executor</code> is an instance of
     * {@link java.util.concurrent.Executor}.
     * <p>
     * <b>Important:</b>
     * <p>
     * If long-running tasks are performed in the callbacks, it is recommended
     * that a custom executor be provided when registering callbacks to ensure
     * that there are enough threads to achieve the desired level of
     * parallelism. By default, the KPL will use an internal thread pool to
     * execute callbacks, but this pool may not have a sufficient number of
     * threads if a large number is desired.
     * <p>
     * Another option would be to hand the result off to a different component
     * for processing and keep the callback routine fast.
     * 
     * @param stream
     *            Stream to put to.
     * @param partitionKey
     *            Partition key. Length must be at least one, and at most 256
     *            (inclusive).
     * @param explicitHashKey
     *            The hash value used to explicitly determine the shard the data
     *            record is assigned to by overriding the partition key hash.
     *            Must be a valid string representation of a positive integer
     *            with value between 0 and <code>2^128 - 1</code> (inclusive).
     * @param data
     *            Binary data of the record. Maximum size 1MiB.
     * @return A future for the result of the put.
     * @throws IllegalArgumentException
     *             if input does not meet stated constraints
     * @throws DaemonException
     *             if the child process is dead
     * @see ListenableFuture
     * @see UserRecordResult
     * @see KinesisProducerConfiguration#setRecordTtl(long)
     * @see UserRecordFailedException
     */
    @Override
    public ListenableFuture<UserRecordResult> addUserRecord(String stream, String partitionKey, String explicitHashKey, ByteBuffer data) {
        return addUserRecord(stream, partitionKey, explicitHashKey, data, null);
    }

    @Override
    public ListenableFuture<UserRecordResult> addUserRecord(String stream, String partitionKey, String explicitHashKey, ByteBuffer data, Schema schema) {
        if (stream == null) {
            throw new IllegalArgumentException("Stream name cannot be null");
        }
        
        stream = stream.trim();
        
        if (stream.length() == 0) {
            throw new IllegalArgumentException("Stream name cannot be empty");
        }
        
        if (partitionKey == null) {
            throw new IllegalArgumentException("partitionKey cannot be null");
        }
        
        if (partitionKey.length() < 1 || partitionKey.length() > 256) {
            throw new IllegalArgumentException(
                    "Invalid partition key. Length must be at least 1 and at most 256, got " + partitionKey.length());
        }
        
        try {
            partitionKey.getBytes("UTF-8");
        } catch (Exception e) {
            throw new IllegalArgumentException("Partition key must be valid UTF-8");
        }
        
        BigInteger b = null;
        if (explicitHashKey != null) {
            explicitHashKey = explicitHashKey.trim();
            try {
                b = new BigInteger(explicitHashKey);
            } catch (NumberFormatException e) {
                throw new IllegalArgumentException("Invalid explicitHashKey, must be an integer, got " + explicitHashKey);
            }
            if (b != null) {
                if (b.compareTo(UINT_128_MAX) > 0 || b.compareTo(BigInteger.ZERO) < 0) {
                    throw new IllegalArgumentException(
                            "Invalid explicitHashKey, must be greater or equal to zero and less than or equal to (2^128 - 1), got " +
                                    explicitHashKey);
                }
            }
        }

        if (schema != null && data != null) {
            if (schema.getSchemaDefinition() == null || schema.getDataFormat() == null) {
                throw new IllegalArgumentException(
                    String.format(
                        "Schema specification is not valid. SchemaDefinition or DataFormat cannot be null. SchemaDefinition: %s, DataFormat: %s",
                        schema.getSchemaDefinition(),
                        schema.getDataFormat()
                    )
                );
            }
            GlueSchemaRegistrySerializer serializer = glueSchemaRegistrySerializerInstance.get(config);
            byte[] encodedBytes = serializer.encode(stream, schema, data.array());
            data = ByteBuffer.wrap(encodedBytes);
        }

        if (data != null && data.remaining() > 1024 * 1024) {
            throw new IllegalArgumentException(
                    "Data must be less than or equal to 1MB in size, got " + data.remaining() + " bytes");
        }
        
        long id = messageNumber.getAndIncrement();
        SettableFuture<UserRecordResult> f = SettableFuture.create();
        FutureTask<String> task = null;
        if(config.getUserRecordTimeoutInMillis() > 0) {
            task = new FutureTask(new FutureTimeoutRunnableTask(id), "TimedOut");
            futureTimeoutExecutor.schedule(task, config.getUserRecordTimeoutInMillis(), TimeUnit.MILLISECONDS);
        }
        SettableFutureTracker futuresTracking = new SettableFutureTracker(f, Instant.now(), Optional.ofNullable(task));
        futures.put(id, futuresTracking);
        oldestFutureTrackerHeap.add(futuresTracking);
        
        PutRecord.Builder pr = PutRecord.newBuilder()
                .setStreamName(stream)
                .setPartitionKey(partitionKey)
                .setData(data != null ? ByteString.copyFrom(data) : ByteString.EMPTY);
        if (b != null) {
            pr.setExplicitHashKey(b.toString(10));
        }
        
        Message m = Message.newBuilder()
                .setId(id)
                .setPutRecord(pr.build())
                .build();
        child.add(m);
        
        return f;
    }

    @AllArgsConstructor
    private class FutureTimeoutRunnableTask implements Runnable {
        private long id;

        @Override
        public void run() {
            SettableFutureTracker futureTracker = getFutureTracker(id);
            totalFutureTimeouts.getAndIncrement();
            SettableFuture<?> f = futureTracker.getFuture();
            String message = "Message id " + id + " timeout out. Removing the submitted future from processing queue.";
            f.setException(new FutureTimedOutException(message));
            log.error(message);
        }
    }
    
    /**
     * Get the number of unfinished records currently being processed. The
     * records could either be waiting to be sent to the child process, or have
     * reached the child process and are being worked on.
     * 
     * <p>
     * This is equal to the number of futures returned from {@link #addUserRecord}
     * that have not finished.
     * 
     * @return The number of unfinished records currently being processed.
     */
    @Override
    public int getOutstandingRecordsCount() {
        return futures.size();
    }

    /**
     * Get the time in millis for the oldest record currently waiting in kpl to be processed for sending to kinesis
     * endpoint. The records could either be waiting to be sent to the child process, or have
     * reached the child process and are being worked on.
     *
     * <p>
     * This is time in millis from the oldest future submitted from {@link #addUserRecord}
     * that have not finished. This returns 0 if there are no pending records in processing state.
     *
     * @return The time in millis since the oldest record is pending to be sent to kinesis endpoint. Returns 0 if
     * there are no records in processing state.
     */
    @Override
    public long getOldestRecordTimeInMillis() {
        SettableFutureTracker oldestFuture = oldestFutureTrackerHeap.peek();
        if (oldestFuture == null) {
            return 0;
        }
        return Instant.now().toEpochMilli() - oldestFuture.getTimestamp().toEpochMilli();
    }

    /**
     * Get metrics from the KPL.
     * 
     * <p>
     * The KPL computes and buffers useful metrics. Use this method to retrieve
     * them. The same metrics are also uploaded to CloudWatch (unless disabled).
     * 
     * <p>
     * Multiple metrics exist for the same name, each with a different list of
     * dimensions (e.g. stream name). This method will fetch all metrics with
     * the provided name.
     * 
     * <p>
     * See the stand-alone metrics documentation for details about individual
     * metrics.
     * 
     * <p>
     * This method is synchronous and will block while the data is being
     * retrieved.
     * 
     * @param metricName
     *            Name of the metrics to fetch.
     * @param windowSeconds
     *            Fetch data from the last N seconds. The KPL maintains data at
     *            per second granularity for the past minute. To get total
     *            cumulative data since the start of the program, use the
     *            overloads that do not take this argument.
     * @return A list of metrics with the provided name.
     * @throws ExecutionException
     *             If an error occurred while fetching metrics from the child
     *             process.
     * @throws InterruptedException
     *             If the thread is interrupted while waiting for the response
     *             from the child process.
     * @see Metric
     */
    @Override
    public List<Metric> getMetrics(String metricName, int windowSeconds) throws InterruptedException, ExecutionException {
        MetricsRequest.Builder mrb = MetricsRequest.newBuilder();
        if (metricName != null) {
            mrb.setName(metricName);
        }
        if (windowSeconds > 0) {
            mrb.setSeconds(windowSeconds);
        }
        
        long id = messageNumber.getAndIncrement();
        SettableFuture<List<Metric>> f = SettableFuture.create();
        FutureTask<String> task = null;
        if(config.getUserRecordTimeoutInMillis() > 0) {
            task = new FutureTask(new FutureTimeoutRunnableTask(id), "TimedOut");
            futureTimeoutExecutor.schedule(task, config.getUserRecordTimeoutInMillis(), TimeUnit.MILLISECONDS);
        }
        SettableFutureTracker futuresTracking = new SettableFutureTracker(f, Instant.now(), Optional.ofNullable(task));
        futures.put(id, futuresTracking);
        oldestFutureTrackerHeap.add(futuresTracking);
        
        child.add(Message.newBuilder()
                .setId(id)
                .setMetricsRequest(mrb.build())
                .build());
        
        return f.get();
    }
    
    /**
     * Get metrics from the KPL.
     * 
     * <p>
     * The KPL computes and buffers useful metrics. Use this method to retrieve
     * them. The same metrics are also uploaded to CloudWatch (unless disabled).
     * 
     * <p>
     * Multiple metrics exist for the same name, each with a different list of
     * dimensions (e.g. stream name). This method will fetch all metrics with
     * the provided name.
     * 
     * <p>
     * The retrieved data represents cumulative statistics since the start of
     * the program. To get data from a smaller window, use
     * {@link #getMetrics(String, int)}.
     * 
     * <p>
     * See the stand-alone metrics documentation for details about individual
     * metrics.
     * 
     * <p>
     * This method is synchronous and will block while the data is being
     * retrieved.
     * 
     * @param metricName
     *            Name of the metrics to fetch.
     * @return A list of metrics with the provided name.
      * @throws ExecutionException
     *             If an error occurred while fetching metrics from the child
     *             process.
     * @throws InterruptedException
     *             If the thread is interrupted while waiting for the response
     *             from the child process.
     * @see Metric
     */
    @Override
    public List<Metric> getMetrics(String metricName) throws InterruptedException, ExecutionException {
        return getMetrics(metricName, -1);
    }
    
    /**
     * Get metrics from the KPL.
     * 
     * <p>
     * The KPL computes and buffers useful metrics. Use this method to retrieve
     * them. The same metrics are also uploaded to CloudWatch (unless disabled).
     * 
     * <p>
     * This method fetches all metrics available. To fetch only metrics with a
     * given name, use {@link #getMetrics(String)}.
     * 
     * <p>
     * The retrieved data represents cumulative statistics since the start of
     * the program. To get data from a smaller window, use
     * {@link #getMetrics(int)}.
     * 
     * <p>
     * See the stand-alone metrics documentation for details about individual
     * metrics.
     * 
     * <p>
     * This method is synchronous and will block while the data is being
     * retrieved.
     * 
     * @return A list of all of the metrics maintained by the KPL.
      * @throws ExecutionException
     *             If an error occurred while fetching metrics from the child
     *             process.
     * @throws InterruptedException
     *             If the thread is interrupted while waiting for the response
     *             from the child process.
     * @see Metric
     */
    @Override
    public List<Metric> getMetrics() throws InterruptedException, ExecutionException {
        return getMetrics(null);
    }
    
    /**
     * Get metrics from the KPL.
     * 
     * <p>
     * The KPL computes and buffers useful metrics. Use this method to retrieve
     * them. The same metrics are also uploaded to CloudWatch (unless disabled).
     * 
     * <p>
     * This method fetches all metrics available. To fetch only metrics with a
     * given name, use {@link #getMetrics(String, int)}.
     * 
     * <p>
     * See the stand-alone metrics documentation for details about individual
     * metrics.
     * 
     * <p>
     * This method is synchronous and will block while the data is being
     * retrieved.
     * 
     * @param windowSeconds
     *            Fetch data from the last N seconds. The KPL maintains data at
     *            per second granularity for the past minute. To get total
     *            cumulative data since the start of the program, use the
     *            overloads that do not take this argument.
     * @return A list of metrics with the provided name.
     * @throws ExecutionException
     *             If an error occurred while fetching metrics from the child
     *             process.
     * @throws InterruptedException
     *             If the thread is interrupted while waiting for the response
     *             from the child process.
     * @see Metric
     */
    @Override
    public List<Metric> getMetrics(int windowSeconds) throws InterruptedException, ExecutionException {
        return getMetrics(null, windowSeconds);
    }

    /**
     * Immediately kill the child process. This will cause all outstanding
     * futures to fail immediately.
     * 
     * <p>
     * To perform a graceful shutdown instead, there are several options:
     * 
     * <ul>
     * <li>Call {@link #flush()} and wait (perhaps with a time limit) for all
     * futures. If you were sending a very high volume of data you may need to
     * call flush multiple times to clear all buffers.</li>
     * <li>Poll {@link #getOutstandingRecordsCount()} until it returns 0.</li>
     * <li>Call {@link #flushSync()}, which blocks until completion.</li>
     * </ul>
     * 
     * Once all records are confirmed with one of the above, call destroy to
     * terminate the child process. If you are terminating the JVM then calling
     * destroy is unnecessary since it will be done automatically.
     */
    @Override
    public void destroy() {
        destroyed = true;
        this.callbackCompletionExecutor.shutdownNow();
        child.destroy();
    }

    /**
     * Instruct the child process to perform a flush, sending some of the
     * records it has buffered for the specified stream.
     * 
     * <p>
     * This does not guarantee that all buffered records will be sent, only that
     * most of them will; to flush all records and wait for completion, use
     * {@link #flushSync}.
     * 
     * <p>
     * This method returns immediately without blocking.
     * 
     * @param stream
     *            Stream to flush
     * @throws DaemonException
     *             if the child process is dead
     */
    @Override
    public void flush(String stream) {
        Flush.Builder f = Flush.newBuilder();
        if (stream != null) {
            f.setStreamName(stream);
        }
        Message m = Message.newBuilder()
                .setId(messageNumber.getAndIncrement())
                .setFlush(f.build())
                .build();
        child.add(m);
    }

    /**
     * Instruct the child process to perform a flush, sending some of the
     * records it has buffered. Applies to all streams.
     * 
     * <p>
     * This does not guarantee that all buffered records will be sent, only that
     * most of them will; to flush all records and wait for completion, use
     * {@link #flushSync}.
     * 
     * <p>
     * This method returns immediately without blocking.
     * 
     * @throws DaemonException
     *             if the child process is dead
     */
    @Override
    public void flush() {
        flush(null);
    }
    
    /**
     * Instructs the child process to flush all records and waits until all
     * records are complete (either succeeding or failing).
     * 
     * <p>
     * The wait includes any retries that need to be performed. Depending on
     * your configuration of record TTL and request timeout, this can
     * potentially take a long time if the library is having trouble delivering
     * records to the backend, for example due to network problems. 
     * 
     * <p>
     * This is useful if you need to shutdown your application and want to make
     * sure all records are delivered before doing so.
     * 
     * @throws DaemonException
     *             if the child process is dead
     * 
     * @see KinesisProducerConfiguration#setRecordTtl(long)
     * @see KinesisProducerConfiguration#setRequestTimeout(long)
     */
    @Override
    public void flushSync() {
        while (getOutstandingRecordsCount() > 0) {
            flush();
            try {
                Thread.sleep(500);
            } catch (InterruptedException e) { }
        }
    }

    private String extractBinaries() {
        synchronized (EXTRACT_BIN_MUTEX) {
            final List<File> watchFiles = new ArrayList<>(2);
            String os = SystemUtils.OS_NAME;
            if (SystemUtils.IS_OS_WINDOWS) {
                os = "windows";
            } else if (SystemUtils.IS_OS_LINUX) {
                os = "linux-" + (SystemUtils.OS_ARCH.equals("amd64") ? "x86_64" : SystemUtils.OS_ARCH);
            } else if (SystemUtils.IS_OS_MAC_OSX) {
                os = "osx";
            } else {
                throw new RuntimeException("Your operation system is not supported (" +
                        os + "), the KPL only supports Linux, OSX and Windows");
            }
            
            String root = "amazon-kinesis-producer-native-binaries";
            String tmpDir = config.getTempDirectory();
            if (tmpDir.trim().length() == 0) {
                tmpDir = System.getProperty("java.io.tmpdir");
            }
            tmpDir = Paths.get(tmpDir, root).toString();
            pathToTmpDir = tmpDir;
            
            String binPath = config.getNativeExecutable();
            if (binPath != null && !binPath.trim().isEmpty()) {
                pathToExecutable = binPath.trim();
                log.warn("Using non-default native binary at " + pathToExecutable);

                File parent = new File(binPath).getParentFile();
                pathToLibDir = parent.getAbsolutePath();
                CertificateExtractor certificateExtractor = new CertificateExtractor();

                try {
                    String caDirectory = certificateExtractor
                            .extractCertificates(parent.getAbsoluteFile());
                    watchFiles.addAll(certificateExtractor.getExtractedCertificates());
                    FileAgeManager.instance().registerFiles(watchFiles);
                    return caDirectory;
                } catch (IOException ioex) {
                    log.error("Exception while extracting certificates.  Returning no CA directory", ioex);
                    return "";
                }
            } else {
                log.info("Extracting binaries to " + tmpDir);
                try {
                    File tmpDirFile = new File(tmpDir);
                    if (!tmpDirFile.exists() && !tmpDirFile.mkdirs()) {
                        throw new IOException("Could not create tmp dir " + tmpDir);
                    }
                    
                    String extension = os.equals("windows") ? ".exe" : "";
                    String executableName = "kinesis_producer" + extension;

                    InputStream is = this.getClass().getClassLoader().getResourceAsStream(root + "/" + os + "/" + executableName);
                    String resultFileFormat = "kinesis_producer_%s" + extension;

                    File extracted = HashedFileCopier.copyFileFrom(is, tmpDirFile, resultFileFormat);
                    watchFiles.add(extracted);
                    extracted.setExecutable(true);
                    pathToExecutable = extracted.getAbsolutePath();

                    CertificateExtractor certificateExtractor = new CertificateExtractor();

                    String caDirectory = certificateExtractor
                            .extractCertificates(new File(pathToTmpDir).getAbsoluteFile());
                    watchFiles.addAll(certificateExtractor.getExtractedCertificates());
                    pathToLibDir = pathToTmpDir;
                    FileAgeManager.instance().registerFiles(watchFiles);
                    return caDirectory;
                } catch (Exception e) {
                    throw new RuntimeException("Could not copy native binaries to temp directory " + tmpDir, e);
                }

            }
        }
    }
}
