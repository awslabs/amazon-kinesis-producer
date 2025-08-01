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

import software.amazon.kinesis.producer.protobuf.Messages.Attempt;
import software.amazon.kinesis.producer.protobuf.Messages.Message;
import software.amazon.kinesis.producer.protobuf.Messages.PutRecord;
import software.amazon.kinesis.producer.protobuf.Messages.PutRecordResult;
import com.amazonaws.services.schemaregistry.common.Schema;
import com.google.common.util.concurrent.FutureCallback;
import com.google.common.util.concurrent.Futures;
import com.google.common.util.concurrent.ListenableFuture;
import com.google.common.util.concurrent.SettableFuture;
import com.google.protobuf.ByteString;
import org.apache.commons.lang3.StringUtils;
import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.mockito.Mockito;
import org.mockserver.integration.ClientAndServer;
import org.mockserver.model.Header;
import org.mockserver.socket.PortFactory;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import software.amazon.awssdk.auth.credentials.AwsBasicCredentials;
import software.amazon.awssdk.auth.credentials.AwsCredentials;
import software.amazon.awssdk.auth.credentials.AwsCredentialsProvider;
import software.amazon.awssdk.auth.credentials.StaticCredentialsProvider;
import software.amazon.awssdk.services.glue.model.DataFormat;

import java.io.UnsupportedEncodingException;
import java.nio.ByteBuffer;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Random;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.TimeoutException;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.stream.Stream;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.fail;
import static org.mockserver.integration.ClientAndServer.startClientAndServer;
import static org.mockserver.model.HttpRequest.request;
import static org.mockserver.model.HttpResponse.response;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.spy;
import static org.mockito.Mockito.doNothing;

public class KinesisProducerTest {
    private static final Logger log = LoggerFactory.getLogger(KinesisProducerTest.class);

    private static final int port = PortFactory.findFreePort();
    private static final int sts_port = PortFactory.findFreePort();
    private ClientAndServer mockServer;
    private ClientAndServer stsMockServer;

    private static class RotatingAwsCredentialsProvider implements AwsCredentialsProvider {
        private final List<AwsCredentials> creds;
        private final AtomicInteger idx = new AtomicInteger(0);

        public RotatingAwsCredentialsProvider(List<AwsCredentials> creds) {
            this.creds = creds;
        }

        @Override
        public AwsCredentials resolveCredentials() {
            return creds.get(idx.getAndIncrement() % creds.size());
        }
    }

    @Before
    public void startServer() {
        mockServer = startClientAndServer(port);
        mockServer.when(
                request()
        ).respond(
                response()
                        .withStatusCode(403)
                        .withBody("{\"status\":\"Expected error\",\"message\":\"test\"}")
        );

        stsMockServer = startClientAndServer(sts_port);
        stsMockServer.when(
                request()
                        .withMethod("POST")
        ).respond(
                response()
                        .withStatusCode(200)
                        .withBody(
                                // https://docs.aws.amazon.com/STS/latest/APIReference/API_GetCallerIdentity.html
                                "<?xml version=\"1.0\" ?>\n" +
                                        "<GetCallerIdentityResponse xmlns=\"https://sts.amazonaws.com/doc/2011-06-15/\">\n" +
                                        "  <GetCallerIdentityResult>\n" +
                                        "  <Arn>arn:aws:iam::123456789012:user/Alice</Arn>\n" +
                                        "  <UserId>AIDACKCEVSQ6C2EXAMPLE</UserId>\n" +
                                        "  <Account>123456789012</Account>\n" +
                                        "  </GetCallerIdentityResult>\n" +
                                        "  <ResponseMetadata>\n" +
                                        "    <RequestId>01234567-89ab-cdef-0123-456789abcdef</RequestId>\n" +
                                        "  </ResponseMetadata>\n" +
                                        // This trailing \0 is important for AWSXMLClient to parse this snippet
                                        "</GetCallerIdentityResponse>\0"
                        )
                        .withHeaders(
                                new Header("Content-Type", "text/xml")
                        )
        );
    }

    @After
    public void stopServer() {
        mockServer.stop();
        stsMockServer.stop();
    }

    @Test
    public void differentCredsForRecordsAndMetrics() throws InterruptedException, ExecutionException {
        final String AKID_A = "AKIAAAAAAAAAAAAAAAAA";
        final String AKID_B = "AKIABBBBBBBBBBBBBBBB";

        final KinesisProducer kp = getProducer(
                StaticCredentialsProvider.create(
                        AwsBasicCredentials.create(AKID_A, StringUtils.repeat("a", 40))),
                StaticCredentialsProvider.create(
                        AwsBasicCredentials.create(AKID_B, StringUtils.repeat("b", 40)))
        );


        final long start = System.nanoTime();
        while (System.nanoTime() - start < 500 * 1000000) {
            kp.addUserRecord("streamName", "partitionKey", ByteBuffer.wrap(new byte[0]));
            kp.flush();
            Thread.sleep(10);
        }

        kp.flushSync();
        Thread.sleep(1000);
        kp.destroy();

        Map<String, AtomicInteger> counts = new HashMap<>();
        counts.put(AKID_A, new AtomicInteger(0));
        counts.put(AKID_B, new AtomicInteger(0));

        Arrays.stream(mockServer.retrieveRecordedRequests(request())).forEach(
                httpRequest -> {
                    String auth = Stream.of("Authorization", "authorization")
                            .map(headKey -> httpRequest.getHeaders().getValues(headKey).toString())
                            .findFirst().get();
                    String host = Stream.of("Host", "host")
                            .map(headKey -> httpRequest.getHeaders().getValues(headKey).toString())
                            .findFirst().get();
                    if (auth.contains(AKID_B)) {
                        assertFalse(host.contains("kinesis"));
                        counts.get(AKID_B).getAndIncrement();
                    } else if (auth.contains(AKID_A)) {
                        assertFalse(host.contains("monitoring"));
                        counts.get(AKID_A).getAndIncrement();
                    } else {
                        fail("Expected AKID(s) not found in auth header");
                    }
                }
        );

        assertTrue(counts.get(AKID_A).get() > 1);
        assertTrue(counts.get(AKID_B).get() > 1);
    }

    @Test
    public void rotatingCredentials() throws InterruptedException, ExecutionException {
        final String AKID_C = "AKIACCCCCCCCCCCCCCCC";
        final String AKID_D = "AKIDDDDDDDDDDDDDDDDD";

        final KinesisProducer kp = getProducer(
                StaticCredentialsProvider.create(
                        AwsBasicCredentials.create(AKID_C, StringUtils.repeat("c", 40))),
                StaticCredentialsProvider.create(
                        AwsBasicCredentials.create(AKID_D, StringUtils.repeat("d", 40)))
        );

        final long start = System.nanoTime();
        while (System.nanoTime() - start < 500 * 1000000) {
            kp.addUserRecord("streamName", "partitionKey", ByteBuffer.wrap(new byte[0]));
            kp.flush();
            Thread.sleep(10);
        }

        kp.flushSync();
        Thread.sleep(1000);
        kp.destroy();

        Map<String, AtomicInteger> counts = new HashMap<String, AtomicInteger>();
        counts.put(AKID_C, new AtomicInteger(0));
        counts.put(AKID_D, new AtomicInteger(0));

        Arrays.stream(mockServer.retrieveRecordedRequests(request())).forEach(
                httpRequest -> {
                    String auth = Stream.of("Authorization", "authorization")
                            .map(headKey -> httpRequest.getHeaders().getValues(headKey).toString())
                            .findFirst().get();
                    if (auth.contains(AKID_C)) {
                        counts.get(AKID_C).getAndIncrement();
                    } else if (auth.contains(AKID_D)) {
                        counts.get(AKID_D).getAndIncrement();
                    } else {
                        fail("Expected AKID(s) not found in auth header");
                    }
                }
        );

        assertTrue(counts.get(AKID_C).get() > 1);
        assertTrue(counts.get(AKID_D).get() > 1);
    }

    @Test
    public void multipleInstances() throws Exception {
        int N = 8;
        final KinesisProducer[] kps = new KinesisProducer[N];
        ExecutorService exec = Executors.newFixedThreadPool(N);
        for (int i = 0; i < N; i++) {
            final int n = i;
            exec.submit(new Runnable() {
                @Override
                public void run() {
                    try {
                        kps[n] = getProducer(null, null);
                    } catch (Exception e) {
                        log.error("Error starting KPL", e);
                    }
                }
            });
        }
        exec.shutdown();
        exec.awaitTermination(30, TimeUnit.SECONDS);
        Thread.sleep(10000);
        for (int i = 0; i < N; i++) {
            assertNotNull(kps[i]);
            assertNotNull(kps[i].getMetrics());
            kps[i].destroy();
        }
    }

    @Test
    public void schemaIntegration_OnInvalidSchema_ThrowsException() {
        KinesisProducer kinesisProducer = getProducer(null, null);
        String stream = "testStream";
        String partitionKey = "partitionKey";
        String hashKey = null;
        ByteBuffer data = ByteBuffer.wrap(new byte[] { 01, 23, 54 });
        String schemaDefinition = null;
        Schema schema = new Schema(schemaDefinition, DataFormat.AVRO.toString(), "testSchema");
        UserRecord userRecord = new UserRecord(stream, partitionKey, hashKey, data, schema);

        try {
            kinesisProducer.addUserRecord(userRecord);
            fail("Failed to throw IllegalArgumentException");
        } catch (Exception e) {
            assertEquals(IllegalArgumentException.class, e.getClass());
            assertTrue(e.getMessage().contains("Schema specification is not valid. SchemaDefinition or DataFormat cannot be null."));
        }
    }

    private KinesisProducer getProducer(KinesisProducerConfiguration cfg, Daemon daemon,
                                        AwsCredentialsProvider provider,
                                        AwsCredentialsProvider metrics_creds_provider) {
        if (provider != null) {
            cfg.setCredentialsProvider(provider);
        }
        if (metrics_creds_provider != null) {
            cfg.setMetricsCredentialsProvider(metrics_creds_provider);
        }
        return new KinesisProducer(cfg, daemon);
    }

    private KinesisProducer getProducer(AwsCredentialsProvider provider, AwsCredentialsProvider metrics_creds_provider) {
        final KinesisProducerConfiguration cfg = buildBasicConfiguration();
        if (provider != null) {
            cfg.setCredentialsProvider(provider);
        }
        if (metrics_creds_provider != null) {
            cfg.setMetricsCredentialsProvider(metrics_creds_provider);
        }
        return new KinesisProducer(cfg);
    }

    public KinesisProducerConfiguration buildBasicConfiguration() {
        return new KinesisProducerConfiguration()
                .setKinesisEndpoint("localhost")
                .setKinesisPort(port)
                .setCloudwatchEndpoint("localhost")
                .setCloudwatchPort(port)
                .setStsEndpoint("localhost")
                .setStsPort(sts_port)
                .setVerifyCertificate(false)
                .setAggregationEnabled(false)
                .setCredentialsRefreshDelay(100)
                .setRegion("us-west-1")
                .setRecordTtl(200)
                .setMetricsUploadDelay(100)
                .setRecordTtl(100)
                .setLogLevel("warning");
    }

    @Test
    public void getOldestRecordTimeInMillisShouldReturn0WhenNoFuturesInHeap() {
        // Given that there are no futures in the heap,
        IKinesisProducer candidate = getProducer(null, null);

        // When we call getOldestRecordTimeInMillis,
        long actual = candidate.getOldestRecordTimeInMillis();

        // Then we expect it to return 0.
        assertEquals(0L, actual);
    }

    @Test
    public void getOldestRecord_ReturnsPendingOldestRecord() {
        // given
        final KinesisProducerConfiguration cfg = new KinesisProducerConfiguration()
                .setKinesisEndpoint("localhost")
                .setKinesisPort(port)
                .setCloudwatchEndpoint("localhost")
                .setCloudwatchPort(port)
                .setStsEndpoint("localhost")
                .setStsPort(sts_port)
                .setVerifyCertificate(false)
                .setAggregationEnabled(false)
                .setCredentialsRefreshDelay(100)
                .setRegion("us-west-1")
                .setRecordTtl(200)
                .setMetricsUploadDelay(100)
                .setRecordTtl(100)
                .setLogLevel("warning")
                .setEnableOldestFutureTracker(true); // oldest future tracker is enabled
        Daemon child = Mockito.mock(Daemon.class);
        IKinesisProducer candidate = getProducer(cfg, child, null, null);

        // when
        candidate.addUserRecord("streamName", "partitionKey", ByteBuffer.wrap(new byte[0]));
        sleep(2000);
        candidate.addUserRecord("streamName", "partitionKey", ByteBuffer.wrap(new byte[0]));

        // then
        assertTrue(candidate.getOldestRecordTimeInMillis() > 0);
        assertEquals(2, candidate.getOutstandingRecordsCount());
    }

    @Test
    public void getOldestRecordTime_ShouldReturn0_WhenConfigIsDisabled() {
        // given
        final KinesisProducerConfiguration cfg = new KinesisProducerConfiguration()
                .setKinesisEndpoint("localhost")
                .setKinesisPort(port)
                .setCloudwatchEndpoint("localhost")
                .setCloudwatchPort(port)
                .setStsEndpoint("localhost")
                .setStsPort(sts_port)
                .setVerifyCertificate(false)
                .setAggregationEnabled(false)
                .setCredentialsRefreshDelay(100)
                .setRegion("us-west-1")
                .setRecordTtl(200)
                .setMetricsUploadDelay(100)
                .setRecordTtl(100)
                .setLogLevel("warning")
                .setEnableOldestFutureTracker(false); // oldest future tracker is disabled
        Daemon child = Mockito.mock(Daemon.class);
        IKinesisProducer candidate = getProducer(cfg, child, null, null);

        // when
        candidate.addUserRecord("streamName", "partitionKey", ByteBuffer.wrap(new byte[0]));
        sleep(2000);
        candidate.addUserRecord("streamName", "partitionKey", ByteBuffer.wrap(new byte[0]));

        // then
        assertEquals(0, candidate.getOldestRecordTimeInMillis());
        assertEquals(2, candidate.getOutstandingRecordsCount());
    }

    @Test
    public void getOldestRecordTime_ForMetrics_ShouldReturn0_WhenConfigIsDisabled() throws ExecutionException, InterruptedException {
        // given
        final KinesisProducerConfiguration cfg = new KinesisProducerConfiguration()
                .setKinesisEndpoint("localhost")
                .setKinesisPort(port)
                .setCloudwatchEndpoint("localhost")
                .setCloudwatchPort(port)
                .setStsEndpoint("localhost")
                .setStsPort(sts_port)
                .setVerifyCertificate(false)
                .setAggregationEnabled(false)
                .setCredentialsRefreshDelay(100)
                .setRegion("us-west-1")
                .setRecordTtl(200)
                .setMetricsUploadDelay(100)
                .setRecordTtl(100)
                .setLogLevel("warning")
                .setEnableOldestFutureTracker(false); // oldest future tracker is disabled
        Daemon child = Mockito.mock(Daemon.class);
        KinesisProducer candidate = getProducer(cfg, child, null, null);

        AtomicBoolean noOldestRecordsInHeap = new AtomicBoolean(true);
        ExecutorService svc = Executors.newSingleThreadExecutor();
        svc.submit(() -> {
            while (true) {
                if (svc.isShutdown()) {
                    break;
                }
                Map<Long, KinesisProducer.SettableFutureTracker> futures = candidate.getFutures();
                if (!futures.isEmpty()) {
                    futures.values().stream().forEach(sft -> {
                        SettableFuture<List<Metric>> f = (SettableFuture<List<Metric>>) sft.getFuture();
                        f.set(new ArrayList<>());
                    });
                }
                if (candidate.getOldestRecordTimeInMillis() > 0) {
                    noOldestRecordsInHeap.set(false);
                }
            }
        });

        // when
        candidate.getMetrics();
        sleep(2000);
        candidate.getMetrics();

        // then
        svc.shutdown();
        assertEquals(0, candidate.getOldestRecordTimeInMillis());
        assertEquals(2, candidate.getOutstandingRecordsCount()); // we complete the future, but don't remove it from futures hence we still see records
        assertTrue(noOldestRecordsInHeap.get());
    }

    @Test
    public void getOldestRecordTime_ForMetrics_ShouldNotReturn0_WhenConfigIsEnabled() throws ExecutionException, InterruptedException {
        // given
        final KinesisProducerConfiguration cfg = new KinesisProducerConfiguration()
                .setKinesisEndpoint("localhost")
                .setKinesisPort(port)
                .setCloudwatchEndpoint("localhost")
                .setCloudwatchPort(port)
                .setStsEndpoint("localhost")
                .setStsPort(sts_port)
                .setVerifyCertificate(false)
                .setAggregationEnabled(false)
                .setCredentialsRefreshDelay(100)
                .setRegion("us-west-1")
                .setRecordTtl(200)
                .setMetricsUploadDelay(100)
                .setRecordTtl(100)
                .setLogLevel("warning")
                .setEnableOldestFutureTracker(true); // oldest future tracker is enabled
        Daemon child = Mockito.mock(Daemon.class);
        KinesisProducer candidate = getProducer(cfg, child, null, null);

        AtomicBoolean oldestRecordsInHeap = new AtomicBoolean(false);
        ExecutorService svc = Executors.newSingleThreadExecutor();
        svc.submit(() -> {
            while (true) {
                if (svc.isShutdown()) {
                    break;
                }
                Map<Long, KinesisProducer.SettableFutureTracker> futures = candidate.getFutures();
                if (!futures.isEmpty()) {
                    futures.values().stream().forEach(sft -> {
                        SettableFuture<List<Metric>> f = (SettableFuture<List<Metric>>) sft.getFuture();
                        f.set(new ArrayList<>());
                    });
                }
                if (candidate.getOldestRecordTimeInMillis() > 0) {
                    oldestRecordsInHeap.set(true);
                }
            }
        });

        // when
        candidate.getMetrics();
        sleep(2000);
        candidate.getMetrics();

        // then
        svc.shutdown();
        assertTrue(candidate.getOldestRecordTimeInMillis() > 0);
        assertEquals(2, candidate.getOutstandingRecordsCount()); // we complete the future, but don't remove it from futures hence we still see records
        assertTrue(oldestRecordsInHeap.get());
    }

    @Test
    public void addUserRecordReturnsRecordOnSuccess() throws UnsupportedEncodingException,
            InterruptedException, ExecutionException, TimeoutException {

        // given
        final KinesisProducerConfiguration cfg = buildBasicConfiguration()
                .setReturnUserRecordInFuture(true);
        final String streamName = "streamName";
        final String partitionKey = "partitionKey";
        final String stringToEncode = "Unit test sample data";
        final KinesisProducer producerSpy = spy(new KinesisProducer(cfg));

        // when
        // skip sending the message to daemon so that we can test receiving the message
        doNothing().when(producerSpy).addMessageToChild(any());
        ByteBuffer data = ByteBuffer.wrap(stringToEncode.getBytes("UTF-8"));
        ListenableFuture<UserRecordResult> f = producerSpy.addUserRecord(streamName, partitionKey, data);

        // Mock a message received from Daemon
        PutRecordResult prr = PutRecordResult.newBuilder()
                .setSuccess(true)
                .setShardId("shard-1")
                .setSequenceNumber("123")
                .build();
        Message m = Message.newBuilder()
                .setId(new Random().nextLong())
                .setSourceId(1L) // first message is always 1
                .setPutRecordResult(prr)
                .build();
        producerSpy.getChild().getHandler().onMessage(m);

        // then
        UserRecordResult userRecordResult = f.get(5, TimeUnit.SECONDS);
        UserRecord userRecord = userRecordResult.getUserRecord();
        assertEquals(streamName, userRecord.getStreamName());
        assertEquals(partitionKey, userRecord.getPartitionKey());
        assertEquals(stringToEncode, StandardCharsets.UTF_8.decode(userRecord.getData())
                .toString());
    }

    @Test
    public void addUserRecordDoesNotReturnsRecordOnSuccessWhenDisabled() throws UnsupportedEncodingException,
            InterruptedException, ExecutionException, TimeoutException {

        // given
        final KinesisProducerConfiguration cfg = buildBasicConfiguration();
        final String streamName = "streamName";
        final String partitionKey = "partitionKey";
        final String stringToEncode = "Unit test sample data";
        final KinesisProducer producerSpy = spy(new KinesisProducer(cfg));

        // when
        // skip sending the message to daemon so that we can test receiving the message
        doNothing().when(producerSpy).addMessageToChild(any());
        ByteBuffer data = ByteBuffer.wrap(stringToEncode.getBytes("UTF-8"));
        ListenableFuture<UserRecordResult> f = producerSpy.addUserRecord(streamName, partitionKey, data);

        // Mock a message received from Daemon
        PutRecordResult prr = PutRecordResult.newBuilder()
                .setSuccess(true)
                .setShardId("shard-1")
                .setSequenceNumber("123")
                .build();
        Message m = Message.newBuilder()
                .setId(new Random().nextLong())
                .setSourceId(1L) // first message is always 1
                .setPutRecordResult(prr)
                .build();
        producerSpy.getChild().getHandler().onMessage(m);

        // then
        UserRecordResult userRecordResult = f.get(5, TimeUnit.SECONDS);
        UserRecord userRecord = userRecordResult.getUserRecord();
        assertNull(streamName, userRecord);
    }

    @Test
    public void addUserRecordReturnsRecordOnFailureInDaemon() throws UnsupportedEncodingException, InterruptedException {

        // given
        final KinesisProducerConfiguration cfg = buildBasicConfiguration()
                .setReturnUserRecordInFuture(true);
        final String streamName = "streamName";
        final String partitionKey = "partitionKey";
        final String stringToEncode = "Unit test sample data";
        final KinesisProducer producerSpy = spy(new KinesisProducer(cfg));

        // when
        // skip sending the message to daemon so that we can test receiving the message
        doNothing().when(producerSpy).addMessageToChild(any());
        ByteBuffer data = ByteBuffer.wrap(stringToEncode.getBytes("UTF-8"));
        ListenableFuture<UserRecordResult> f = producerSpy.addUserRecord(streamName, partitionKey, data);

        // Mock an error thrown from Daemon
        producerSpy.getChild().getHandler().onError(new DaemonException("Mocked exception."));

        // then
        try {
            f.get(5, TimeUnit.SECONDS);
            fail("Expected an exception");
        } catch (TimeoutException e) {
            fail("Future timed out unexpectedly");
        } catch (ExecutionException e) {
            if (e.getCause() instanceof KinesisProducerException) {
                UserRecord userRecord = ((KinesisProducerException) e.getCause()).getUserRecord();
                assertEquals(streamName, userRecord.getStreamName());
                assertEquals(partitionKey, userRecord.getPartitionKey());
                assertEquals(stringToEncode, StandardCharsets.UTF_8.decode(userRecord.getData())
                        .toString());
            } else {
                fail(String.format("Got unexpected exception: {}", e));
            }
        }
    }

    @Test
    public void addUserRecordReturnsRecordOnFailureUserRecordFailedException() throws UnsupportedEncodingException, InterruptedException {

        // given
        final KinesisProducerConfiguration cfg = buildBasicConfiguration()
                .setReturnUserRecordInFuture(true);
        final String streamName = "streamName";
        final String partitionKey = "partitionKey";
        final String stringToEncode = "Unit test sample data";
        final KinesisProducer producerSpy = spy(new KinesisProducer(cfg));

        // when
        // skip sending the message to daemon so that we can test receiving the message
        doNothing().when(producerSpy).addMessageToChild(any());
        ByteBuffer data = ByteBuffer.wrap(stringToEncode.getBytes("UTF-8"));
        ListenableFuture<UserRecordResult> f = producerSpy.addUserRecord(streamName, partitionKey, data);

        // Mock a message received from Daemon
        PutRecordResult prr = PutRecordResult.newBuilder()
                .setSuccess(false)
                .setShardId("shard-1")
                .setSequenceNumber("123")
                .build();
        Message m = Message.newBuilder()
                .setId(new Random().nextLong())
                .setSourceId(1L) // first message is always 1
                .setPutRecordResult(prr)
                .build();
        producerSpy.getChild().getHandler().onMessage(m);

        // then
        try {
            f.get(5, TimeUnit.SECONDS);
            fail("Expected an exception");
        } catch (TimeoutException e) {
            fail("Future timed out unexpectedly");
        } catch (ExecutionException e) {
            if (e.getCause() instanceof UserRecordFailedException) {
                UserRecord userRecord = ((KinesisProducerException) e.getCause()).getUserRecord();
                assertEquals(streamName, userRecord.getStreamName());
                assertEquals(partitionKey, userRecord.getPartitionKey());
                assertEquals(stringToEncode, StandardCharsets.UTF_8.decode(userRecord.getData())
                        .toString());
            } else {
                fail(String.format("Got unexpected exception: {}", e));
            }
        }
    }

    @Test
    public void addUserRecordReturnsRecordOnFailureFutureTimedOutException() throws UnsupportedEncodingException, InterruptedException {

        // given
        final KinesisProducerConfiguration cfg = buildBasicConfiguration()
                .setReturnUserRecordInFuture(true)
                .setUserRecordTimeoutInMillis(1000); // make it timeout
        final String streamName = "streamName";
        final String partitionKey = "partitionKey";
        final String stringToEncode = "Unit test sample data";
        final KinesisProducer producerSpy = spy(new KinesisProducer(cfg));

        // when
        // skip sending the message to daemon so that we can test receiving the message
        doNothing().when(producerSpy).addMessageToChild(any());
        ByteBuffer data = ByteBuffer.wrap(stringToEncode.getBytes(StandardCharsets.UTF_8));
        ListenableFuture<UserRecordResult> f = producerSpy.addUserRecord(streamName, partitionKey, data);
        // Sleep so the UserRecord times out
        sleep(2000);

        // then
        try {
            f.get(5, TimeUnit.SECONDS);
            fail("Expected an exception");
        } catch (TimeoutException e) {
            fail("Future timed out unexpectedly");
        } catch (ExecutionException e) {
            if (e.getCause() instanceof FutureTimedOutException) {
                UserRecord userRecord = ((KinesisProducerException) e.getCause()).getUserRecord();
                assertEquals(streamName, userRecord.getStreamName());
                assertEquals(partitionKey, userRecord.getPartitionKey());
                assertEquals(stringToEncode, StandardCharsets.UTF_8.decode(userRecord.getData())
                        .toString());
            } else {
                fail(String.format("Got unexpected exception: {}", e));
            }
        }
    }

    @Test
    public void addUserRecordReturnsRecordOnFailureUnexpectedMessageException() throws UnsupportedEncodingException, InterruptedException {

        // given
        final KinesisProducerConfiguration cfg = buildBasicConfiguration()
                .setReturnUserRecordInFuture(true);
        final String streamName = "streamName";
        final String partitionKey = "partitionKey";
        final String stringToEncode = "Unit test sample data";
        final KinesisProducer producerSpy = spy(new KinesisProducer(cfg));

        // when
        // skip sending the message to daemon so that we can test receiving the message
        doNothing().when(producerSpy).addMessageToChild(any());
        ByteBuffer data = ByteBuffer.wrap(stringToEncode.getBytes(StandardCharsets.UTF_8));
        ListenableFuture<UserRecordResult> f = producerSpy.addUserRecord(streamName, partitionKey, data);

        // Mock an unexpected message received from Daemon
        PutRecord pr = PutRecord.newBuilder()
                .setStreamName(streamName)
                .setPartitionKey(partitionKey)
                .setData(ByteString.copyFrom(data))
                .build();
        Message m = Message.newBuilder()
                .setId(new Random().nextLong())
                .setSourceId(1L) // first message is always 1
                .setPutRecord(pr)
                .build();
        producerSpy.getChild().getHandler().onMessage(m);

        try {
            f.get(5, TimeUnit.SECONDS);
            fail("Expected an exception");
        } catch (TimeoutException e) {
            fail("Future timed out unexpectedly");
        } catch (ExecutionException e) {
            if (e.getCause() instanceof UnexpectedMessageException) {
                UserRecord userRecord = ((KinesisProducerException) e.getCause()).getUserRecord();
                assertEquals(streamName, userRecord.getStreamName());
                assertEquals(partitionKey, userRecord.getPartitionKey());
                assertEquals(stringToEncode, StandardCharsets.UTF_8.decode(userRecord.getData()).toString());
            } else {
                fail(String.format("Got unexpected exception: {}", e));
            }
        }
    }

    private void sleep(long millisToSleep) {
        try {
            Thread.sleep(millisToSleep);
        } catch (InterruptedException e) {
            throw new RuntimeException(e);
        }
    }
}
