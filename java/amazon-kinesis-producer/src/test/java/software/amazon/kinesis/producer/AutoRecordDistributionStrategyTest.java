package software.amazon.kinesis.producer;

import org.junit.AfterClass;
import org.junit.BeforeClass;
import org.junit.Test;
import org.mockserver.integration.ClientAndServer;
import org.mockserver.matchers.TimeToLive;
import org.mockserver.matchers.Times;
import org.mockserver.model.HttpRequest;
import org.mockserver.socket.PortFactory;

import java.nio.ByteBuffer;

import static org.junit.Assert.assertTrue;
import static org.mockserver.integration.ClientAndServer.startClientAndServer;
import static org.mockserver.model.HttpRequest.request;
import static org.mockserver.model.HttpResponse.response;

/**
 * Full-stack tests for RecordDistributionStrategy=AUTO: a real native daemon
 * driven against a mock Kinesis endpoint. Exercises Java API -> IPC -> daemon ->
 * real AWS SDK -> (mocked) HTTP -> response parsing -> strategy discovery ->
 * record routing -> result callback, plus the StreamStrategyUpdate push back to
 * the Java strategy cache.
 *
 * One mock server and one daemon (KinesisProducer) are shared across all cases
 * to keep the number of concurrently-spawned daemons low (the suite also runs
 * multipleInstances, which spawns 8). Each case uses a distinct stream name, and
 * DescribeStreamSummary responses are matched by the stream name in the request
 * body, so a single daemon serves every case.
 *
 * Mock matching: operation-specific expectations use priority 10 so they always
 * win over the priority-0 catch-all, regardless of registration order.
 */
public class AutoRecordDistributionStrategyTest {
    private static final int port = PortFactory.findFreePort();
    private static final int sts_port = PortFactory.findFreePort();

    private static final String DESCRIBE_TARGET = "Kinesis_20131202.DescribeStreamSummary";
    private static final String PUT_RECORDS_TARGET = "Kinesis_20131202.PutRecords";

    // Distinct stream per case so the shared mock/daemon can serve all of them.
    private static final String AUTO_STREAM = "autoStream";
    private static final String UPK_STREAM = "upkStream";
    private static final String DEGRADE_STREAM = "degradeStream";

    private static ClientAndServer mockServer;
    private static KinesisProducer kp;

    @BeforeClass
    public static void setUp() {
        mockServer = startClientAndServer(port);

        // PutRecords -> success (single-record batches in these tests).
        mockServer.when(
                request().withHeader("X-Amz-Target", PUT_RECORDS_TARGET),
                Times.unlimited(), TimeToLive.unlimited(), 10
        ).respond(
                response().withStatusCode(200)
                        .withHeader("Content-Type", "application/x-amz-json-1.1")
                        .withHeader("x-amzn-RequestId", "put-req")
                        .withBody("{\"FailedRecordCount\":0,\"Records\":["
                                + "{\"SequenceNumber\":\"1\",\"ShardId\":\"shardId-000000000000\"}]}")
        );

        // Per-stream DescribeStreamSummary responses, matched by the stream name
        // in the request body.
        describeReturns(AUTO_STREAM, "AUTO");
        describeReturns(UPK_STREAM, "USER_PARTITION_KEY");
        // DEGRADE_STREAM: DescribeStreamSummary fails, so discovery can't resolve.
        mockServer.when(
                request().withHeader("X-Amz-Target", DESCRIBE_TARGET)
                        .withBody(org.mockserver.model.StringBody.subString(DEGRADE_STREAM)),
                Times.unlimited(), TimeToLive.unlimited(), 10
        ).respond(
                response().withStatusCode(500)
                        .withHeader("Content-Type", "application/x-amz-json-1.1")
                        .withHeader("x-amzn-RequestId", "describe-err")
                        .withBody("{\"__type\":\"InternalFailure\",\"message\":\"boom\"}")
        );

        // Catch-all (CloudWatch, STS, etc.) -> harmless 200, lowest priority.
        mockServer.when(request()).respond(response().withStatusCode(200).withBody("{}"));

        KinesisProducerConfiguration cfg = new KinesisProducerConfiguration()
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
                .setRecordTtl(2000)
                .setMetricsUploadDelay(100)
                .setLogLevel("info");
        kp = new KinesisProducer(cfg);
    }

    @AfterClass
    public static void tearDown() {
        if (kp != null) {
            kp.destroy();
        }
        if (mockServer != null) {
            mockServer.stop();
        }
    }

    private static void describeReturns(String stream, String strategy) {
        mockServer.when(
                request().withHeader("X-Amz-Target", DESCRIBE_TARGET)
                        .withBody(org.mockserver.model.StringBody.subString(stream)),
                Times.unlimited(), TimeToLive.unlimited(), 10
        ).respond(
                response().withStatusCode(200)
                        .withHeader("Content-Type", "application/x-amz-json-1.1")
                        .withHeader("x-amzn-RequestId", "describe-req")
                        .withBody("{\"StreamDescriptionSummary\":{"
                                + "\"StreamName\":\"" + stream + "\","
                                + "\"StreamARN\":\"arn:aws:kinesis:us-west-1:123456789012:stream/" + stream + "\","
                                + "\"StreamStatus\":\"ACTIVE\","
                                + "\"RetentionPeriodHours\":24,"
                                + "\"StreamCreationTimestamp\":1700000000,"
                                + "\"EnhancedMonitoring\":[],"
                                + "\"OpenShardCount\":1,"
                                + "\"RecordDistributionStrategy\":\"" + strategy + "\"}}")
        );
    }

    private boolean daemonCalledFor(String target, String streamSubstring) {
        for (HttpRequest r : mockServer.retrieveRecordedRequests(request())) {
            if (target.equals(r.getFirstHeader("X-Amz-Target"))
                    && r.getBodyAsString() != null
                    && r.getBodyAsString().contains(streamSubstring)) {
                return true;
            }
        }
        return false;
    }

    // 1. AUTO discovered -> a null-PK record is accepted and delivered.
    @Test
    public void autoStream_nullPk_delivered() throws Exception {
        UserRecordResult result =
                kp.addUserRecord(AUTO_STREAM, ByteBuffer.wrap(new byte[] {1, 2, 3})).get();
        assertTrue("null-PK record should be delivered on an AUTO stream", result.isSuccessful());
        assertTrue(daemonCalledFor(DESCRIBE_TARGET, AUTO_STREAM));
        assertTrue(daemonCalledFor(PUT_RECORDS_TARGET, AUTO_STREAM));
    }

    // 2. AUTO discovered -> a record WITH a partition key is still delivered.
    @Test
    public void autoStream_withPk_delivered() throws Exception {
        UserRecordResult result =
                kp.addUserRecord(AUTO_STREAM, "myKey", ByteBuffer.wrap(new byte[] {1, 2, 3})).get();
        assertTrue("PK record should be delivered on an AUTO stream", result.isSuccessful());
    }

    // 3. USER_PARTITION_KEY discovered -> the StreamStrategyUpdate propagates back
    //    to Java, and a subsequent null-PK record is rejected client-side.
    @Test
    public void userPartitionKeyStream_nullPk_rejectedAfterDiscovery() throws Exception {
        // First record (valid PK) triggers discovery; the daemon resolves
        // USER_PARTITION_KEY and pushes a StreamStrategyUpdate back to Java.
        kp.addUserRecord(UPK_STREAM, "validKey", ByteBuffer.wrap(new byte[] {1})).get();
        assertTrue("daemon should have described upkStream", daemonCalledFor(DESCRIBE_TARGET, UPK_STREAM));

        // Wait for the strategy update to propagate into the Java cache, then
        // confirm a null-PK record is rejected synchronously.
        long deadline = System.currentTimeMillis() + 5000;
        boolean rejected = false;
        while (System.currentTimeMillis() < deadline) {
            try {
                kp.addUserRecord(UPK_STREAM, ByteBuffer.wrap(new byte[] {2}));
            } catch (IllegalArgumentException e) {
                rejected = e.getMessage().contains("partitionKey cannot be null");
                break;
            }
            Thread.sleep(50);
        }
        assertTrue("null PK should be rejected once the stream is known to be USER_PARTITION_KEY",
                rejected);
    }

    // 4. DescribeStreamSummary fails -> the stream stays UNKNOWN (no aggregation),
    //    and records still flow (graceful degradation).
    @Test
    public void describeFails_recordStillFlows() throws Exception {
        UserRecordResult result =
                kp.addUserRecord(DEGRADE_STREAM, ByteBuffer.wrap(new byte[] {1, 2, 3})).get();
        assertTrue("record should still flow when discovery fails", result.isSuccessful());
        assertTrue(daemonCalledFor(PUT_RECORDS_TARGET, DEGRADE_STREAM));
    }
}
