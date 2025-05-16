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

import com.amazonaws.services.schemaregistry.common.Schema;
import org.apache.commons.lang3.StringUtils;
import org.junit.After;
import org.junit.Before;
import org.junit.Test;
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

import java.nio.ByteBuffer;
import java.util.Arrays;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.stream.Stream;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.fail;
import static org.mockserver.integration.ClientAndServer.startClientAndServer;
import static org.mockserver.model.HttpRequest.request;
import static org.mockserver.model.HttpResponse.response;

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

    private KinesisProducer getProducer(AwsCredentialsProvider provider, AwsCredentialsProvider metrics_creds_provider) {
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
                .setLogLevel("warning");
        if (provider != null) {
            cfg.setCredentialsProvider(provider);
        }
        if (metrics_creds_provider != null) {
            cfg.setMetricsCredentialsProvider(metrics_creds_provider);
        }
        return new KinesisProducer(cfg);
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
}
