/*
 * Copyright 2015 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * Licensed under the Amazon Software License (the "License").
 * You may not use this file except in compliance with the License.
 * A copy of the License is located at
 *
 * http://aws.amazon.com/asl/
 *
 * or in the "license" file accompanying this file. This file is distributed
 * on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
 * express or implied. See the License for the specific language governing
 * permissions and limitations under the License.
 */

package com.amazonaws.services.kinesis.producer;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.math.BigInteger;
import java.nio.ByteBuffer;
import java.nio.channels.FileLock;
import java.nio.file.Paths;
import java.security.MessageDigest;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.LinkedBlockingQueue;
import java.util.concurrent.RejectedExecutionHandler;
import java.util.concurrent.ThreadPoolExecutor;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicLong;

import javax.xml.bind.DatatypeConverter;

import org.apache.commons.io.IOUtils;
import org.apache.commons.lang.StringUtils;
import org.apache.commons.lang.SystemUtils;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.amazonaws.services.kinesis.producer.protobuf.Messages;
import com.amazonaws.services.kinesis.producer.protobuf.Messages.Flush;
import com.amazonaws.services.kinesis.producer.protobuf.Messages.Message;
import com.amazonaws.services.kinesis.producer.protobuf.Messages.MetricsRequest;
import com.amazonaws.services.kinesis.producer.protobuf.Messages.MetricsResponse;
import com.amazonaws.services.kinesis.producer.protobuf.Messages.PutRecord;
import com.google.common.collect.ImmutableMap;
import com.google.common.util.concurrent.ListenableFuture;
import com.google.common.util.concurrent.SettableFuture;
import com.google.common.util.concurrent.ThreadFactoryBuilder;
import com.google.protobuf.ByteString;

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
    
    private static final AtomicInteger callbackCompletionPoolNumber = new AtomicInteger(0);
    
    private final KinesisProducerConfiguration config;
    private final Map<String, String> env;
    private final AtomicLong messageNumber = new AtomicLong(1);
    private final Map<Long, SettableFuture<?>> futures = new ConcurrentHashMap<>();
      
    private final ExecutorService callbackCompletionExecutor = new ThreadPoolExecutor(
            1,
            Runtime.getRuntime().availableProcessors() * 4,
            5,
            TimeUnit.MINUTES,
            new LinkedBlockingQueue<Runnable>(),
            new ThreadFactoryBuilder()
                    .setDaemon(true)
                    .setNameFormat("kpl-callback-pool-" + callbackCompletionPoolNumber.getAndIncrement() + "-thread-%d")
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
            callbackCompletionExecutor.submit(new Runnable() {
                @Override
                public void run() {
                    if (m.hasPutRecordResult()) {
                        onPutRecordResult(m);
                    } else if (m.hasMetricsResponse()) {
                        onMetricsResponse(m);
                    } else {
                        log.error("Unexpected message type from child process");
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
            for (final Map.Entry<Long, SettableFuture<?>> entry : futures.entrySet()) {
                callbackCompletionExecutor.submit(new Runnable() {
                    @Override
                    public void run() {
                        entry.getValue().setException(t);
                    }
                });
            }
            futures.clear();

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
            SettableFuture<UserRecordResult> f = getFuture(msg);
            UserRecordResult result = UserRecordResult.fromProtobufMessage(msg.getPutRecordResult());
            if (result.isSuccessful()) {
                f.set(result);
            } else {
                f.setException(new UserRecordFailedException(result));
            }
        }
        
        private void onMetricsResponse(Message msg) {
            SettableFuture<List<Metric>> f = getFuture(msg);
            
            List<Metric> userMetrics = new ArrayList<>();
            MetricsResponse res = msg.getMetricsResponse();
            for (Messages.Metric metric : res.getMetricsList()) {
                userMetrics.add(new Metric(metric));
            }
            
            f.set(userMetrics);
        }
        
        private <T> SettableFuture<T> getFuture(Message msg) {
            long id = msg.getSourceId();
            @SuppressWarnings("unchecked")
            SettableFuture<T> f = (SettableFuture<T>) futures.remove(id);
            if (f == null) {
                throw new RuntimeException("Future for message id " + id + " not found");
            }
            return f;
        }
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
        
        extractBinaries();
        
        env = new ImmutableMap.Builder<String, String>()
                .put("LD_LIBRARY_PATH", pathToLibDir)
                .put("DYLD_LIBRARY_PATH", pathToLibDir)
                .put("CA_DIR", pathToTmpDir)
                .build();
        
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

    @Override
	public ListenableFuture<UserRecordResult> addUserRecord(String stream, String partitionKey, ByteBuffer data) {
        return addUserRecord(stream, partitionKey, null, data);
    }

    @Override
	public ListenableFuture<UserRecordResult> addUserRecord(String stream, String partitionKey, String explicitHashKey, ByteBuffer data) {
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
                    "Invalid parition key. Length must be at least 1 and at most 256, got " + partitionKey.length());
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
        
        if (data != null && data.remaining() > 1024 * 1024) {
            throw new IllegalArgumentException(
                    "Data must be less than or equal to 1MB in size, got " + data.remaining() + " bytes");
        }
        
        long id = messageNumber.getAndIncrement();
        SettableFuture<UserRecordResult> f = SettableFuture.create();
        futures.put(id, f);
        
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

    @Override
	public int getOutstandingRecordsCount() {
        return futures.size();
    }

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
        futures.put(id, f);
        
        child.add(Message.newBuilder()
                .setId(id)
                .setMetricsRequest(mrb.build())
                .build());
        
        return f.get();
    }

    @Override
	public List<Metric> getMetrics(String metricName) throws InterruptedException, ExecutionException {
        return getMetrics(metricName, -1);
    }

    @Override
	public List<Metric> getMetrics() throws InterruptedException, ExecutionException {
        return getMetrics(null);
    }

    @Override
	public List<Metric> getMetrics(int windowSeconds) throws InterruptedException, ExecutionException {
        return getMetrics(null, windowSeconds);
    }

    @Override
	public void destroy() {
        destroyed = true;
        child.destroy();
    }

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

    @Override
	public void flush() {
        flush(null);
    }

    @Override
	public void flushSync() {
        while (getOutstandingRecordsCount() > 0) {
            flush();
            try {
                Thread.sleep(500);
            } catch (InterruptedException e) { }
        }
    }

    private void extractBinaries() {
        synchronized (EXTRACT_BIN_MUTEX) {
            final List<File> watchFiles = new ArrayList<>(2);
            String os = SystemUtils.OS_NAME;
            if (SystemUtils.IS_OS_WINDOWS) {
                os = "windows";
            } else if (SystemUtils.IS_OS_LINUX) {
                os = "linux";
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
                pathToLibDir = "";
            } else {
                log.info("Extracting binaries to " + tmpDir);
                try {
                    File tmpDirFile = new File(tmpDir);
                    if (!tmpDirFile.exists() && !tmpDirFile.mkdirs()) {
                        throw new IOException("Could not create tmp dir " + tmpDir);
                    }
                    
                    String extension = os.equals("windows") ? ".exe" : "";
                    String executableName = "kinesis_producer" + extension;
                    byte[] bin = IOUtils.toByteArray(
                            this.getClass().getClassLoader().getResourceAsStream(root + "/" + os + "/" + executableName));
                    MessageDigest md = MessageDigest.getInstance("SHA1");
                    String mdHex = DatatypeConverter.printHexBinary(md.digest(bin)).toLowerCase();
                    
                    pathToExecutable = Paths.get(pathToTmpDir, "kinesis_producer_" + mdHex + extension).toString();
                    File extracted = new File(pathToExecutable);
                    watchFiles.add(extracted);
                    if (extracted.exists()) {
                        try (FileInputStream fis = new FileInputStream(extracted);
                                FileLock lock = fis.getChannel().lock(0, Long.MAX_VALUE, true)) {
                            boolean contentEqual = false;
                            if (extracted.length() == bin.length) {
                                byte[] existingBin = IOUtils.toByteArray(new FileInputStream(extracted));
                                contentEqual = Arrays.equals(bin, existingBin);
                            }
                            if (!contentEqual) {
                                throw new SecurityException("The contents of the binary " + extracted.getAbsolutePath()
                                        + " is not what it's expected to be.");
                            }
                        }
                    } else {
                        try (FileOutputStream fos = new FileOutputStream(extracted);
                                FileLock lock = fos.getChannel().lock()) {
                            IOUtils.write(bin, fos);
                        }
                        extracted.setExecutable(true);
                    }
                    
                    String certFileName = "b204d74a.0";
                    File certFile = new File(pathToTmpDir, certFileName);
                    if (!certFile.exists()) {
                        try (FileOutputStream fos = new FileOutputStream(certFile);
                                FileLock lock = fos.getChannel().lock()) {
                            byte[] certs = IOUtils.toByteArray(
                                    this.getClass().getClassLoader().getResourceAsStream("cacerts/" + certFileName));
                            IOUtils.write(certs, fos);
                        }
                    }

                    watchFiles.add(certFile);
                    pathToLibDir = pathToTmpDir;
                    FileAgeManager.instance().registerFiles(watchFiles);
                } catch (Exception e) {
                    throw new RuntimeException("Could not copy native binaries to temp directory " + tmpDir, e);
                }

            }
        }
    }
}
