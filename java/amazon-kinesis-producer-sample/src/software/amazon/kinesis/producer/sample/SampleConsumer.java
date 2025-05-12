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

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.UUID;
import java.util.concurrent.Executors;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicLong;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import software.amazon.awssdk.regions.Region;
import software.amazon.awssdk.services.cloudwatch.CloudWatchAsyncClient;
import software.amazon.awssdk.services.dynamodb.DynamoDbAsyncClient;
import software.amazon.awssdk.services.kinesis.KinesisAsyncClient;
import software.amazon.kinesis.common.ConfigsBuilder;
import software.amazon.kinesis.common.InitialPositionInStream;
import software.amazon.kinesis.common.InitialPositionInStreamExtended;
import software.amazon.kinesis.common.KinesisClientUtil;
import software.amazon.kinesis.coordinator.Scheduler;
import software.amazon.kinesis.lifecycle.ShutdownReason;
import software.amazon.kinesis.lifecycle.events.InitializationInput;
import software.amazon.kinesis.lifecycle.events.LeaseLostInput;
import software.amazon.kinesis.lifecycle.events.ProcessRecordsInput;
import software.amazon.kinesis.lifecycle.events.ShardEndedInput;
import software.amazon.kinesis.lifecycle.events.ShutdownRequestedInput;
import software.amazon.kinesis.processor.RecordProcessorCheckpointer;
import software.amazon.kinesis.processor.ShardRecordProcessor;
import software.amazon.kinesis.processor.ShardRecordProcessorFactory;
import software.amazon.kinesis.processor.SingleStreamTracker;
import software.amazon.kinesis.retrieval.KinesisClientRecord;

/**
 * If you haven't looked at {@link SampleProducer}, do so first.
 *
 * <p>
 * As mentioned in SampleProducer, we will check that all records are received
 * correctly by the KCL by verifying that there are no gaps in the sequence
 * numbers.
 *
 * <p>
 * As the consumer runs, it will periodically log a message indicating the
 * number of gaps it found in the sequence numbers. A gap is when the difference
 * between two consecutive elements in the sorted list of seen sequence numbers
 * is greater than 1.
 *
 * <p>
 * Over time the number of gaps should converge to 0. You should also observe
 * that the range of sequence numbers seen is equal to the number of records put
 * by the SampleProducer.
 *
 * <p>
 * If the stream contains data from multiple runs of SampleProducer, you should
 * observe the SampleConsumer detecting this and resetting state to only count
 * the latest run.
 *
 * <p>
 * Note if you kill the SampleConsumer halfway and run it again, the number of
 * gaps may never converge to 0. This is because checkpoints may have been made
 * such that some records from the producer's latest run are not processed
 * again. If you observe this, simply run the producer to completion again
 * without terminating the consumer.
 *
 * <p>
 * The consumer continues running until manually terminated, even if there are
 * no more records to consume.
 *
 * @author chaodeng
 * @see SampleProducer
 */
public class SampleConsumer implements ShardRecordProcessorFactory {
    private static final Logger log = LoggerFactory.getLogger(SampleConsumer.class);

    // All records from a run of the producer have the same timestamp in their
    // partition keys. Since this value increases for each run, we can use it
    // determine which run is the latest and disregard data from earlier runs.
    private final AtomicLong largestTimestamp = new AtomicLong(0);

    // List of record sequence numbers we have seen so far.
    private final List<Long> sequenceNumbers = new ArrayList<>();

    // A mutex for largestTimestamp and sequenceNumbers. largestTimestamp is
    // nevertheless an AtomicLong because we cannot capture non-final variables
    // in the child class.
    private final Object lock = new Object();

    @Override
    public ShardRecordProcessor shardRecordProcessor() {
        return new RecordProcessor();
    }

    /**
     * One instance of RecordProcessor is created for every shard in the stream.
     * All instances of RecordProcessor share state by capturing variables from
     * the enclosing SampleConsumer instance. This is a simple way to combine
     * the data from multiple shards.
     */
    private class RecordProcessor implements ShardRecordProcessor {
        @Override
        public void initialize(InitializationInput initializationInput) {
        }

        @Override
        public void processRecords(ProcessRecordsInput processRecordsInput) {
            long timestamp = 0;
            List<Long> seqNos = new ArrayList<>();

            for (KinesisClientRecord r : processRecordsInput.records()) {
                // Get the timestamp of this run from the partition key.
                timestamp = Math.max(timestamp, Long.parseLong(r.partitionKey()));

                // Extract the sequence number. It's encoded as a decimal
                // string and placed at the beginning of the record data,
                // followed by a space. The rest of the record data is padding
                // that we will simply discard.
                try {
                    byte[] b = new byte[r.data().remaining()];
                    r.data().get(b);
                    seqNos.add(Long.parseLong(new String(b, "UTF-8").split(" ")[0]));
                } catch (Exception e) {
                    log.error("Error parsing record", e);
                    System.exit(1);
                }
            }

            synchronized (lock) {
                if (largestTimestamp.get() < timestamp) {
                    log.info(String.format(
                            "Found new larger timestamp: %d (was %d), clearing state",
                            timestamp, largestTimestamp.get()));
                    largestTimestamp.set(timestamp);
                    sequenceNumbers.clear();
                }

                // Only add to the shared list if our data is from the latest run.
                if (largestTimestamp.get() == timestamp) {
                    sequenceNumbers.addAll(seqNos);
                    Collections.sort(sequenceNumbers);
                }
            }

            try {
                processRecordsInput.checkpointer().checkpoint();
            } catch (Exception e) {
                log.error("Error while trying to checkpoint during ProcessRecords", e);
            }
        }

        private void checkpoint(RecordProcessorCheckpointer checkpointer, ShutdownReason reason) {
            log.info("Checkpointing, reason: {}", reason);
            try {
                checkpointer.checkpoint();
            } catch (Exception e) {
                log.error("Error while trying to checkpoint during Shutdown", e);
            }
        }

        @Override
        public void leaseLost(LeaseLostInput leaseLostInput) {
        }

        @Override
        public void shardEnded(ShardEndedInput shardEndedInput) {
            checkpoint(shardEndedInput.checkpointer(), ShutdownReason.SHARD_END);
        }

        @Override
        public void shutdownRequested(ShutdownRequestedInput shutdownRequestedInput) {
            checkpoint(shutdownRequestedInput.checkpointer(), ShutdownReason.REQUESTED);
        }
    }

    /**
     * Log a message indicating the current state.
     */
    public void logResults() {
        synchronized (lock) {
            if (largestTimestamp.get() == 0) {
                return;
            }

            if (sequenceNumbers.size() == 0) {
                log.info("No sequence numbers found for current run.");
                return;
            }

            // The producer assigns sequence numbers starting from 1, so we
            // start counting from one before that, i.e. 0.
            long last = 0;
            long gaps = 0;
            for (long sn : sequenceNumbers) {
                if (sn - last > 1) {
                    gaps++;
                }
                last = sn;
            }

            log.info(String.format(
                    "Found %d gaps in the sequence numbers. Lowest seen so far is %d, highest is %d",
                    gaps, sequenceNumbers.get(0), sequenceNumbers.get(sequenceNumbers.size() - 1)));
        }
    }

    /** The main method.
     *  @param args  The command line args for the Sample Producer.
     *  The main method takes 2 optional position parameters:
     *  1. The stream name to use (test is default)
     *  2. The region name to use (us-west-1 in default)
     */
    public static void main(String[] args) {
        int argIndex=0;
        final String streamName = SampleProducerConfig.getArgIfPresent(args, argIndex++, SampleProducerConfig.STREAM_NAME_DEFAULT);
        final Region region = Region.of(SampleProducerConfig.getArgIfPresent(args, argIndex++, SampleProducerConfig.REGION_DEFAULT));

        final SampleConsumer consumer = new SampleConsumer();
        final KinesisAsyncClient kinesisClient = KinesisClientUtil.createKinesisAsyncClient(KinesisAsyncClient.builder().region(region));
        final DynamoDbAsyncClient dynamoClient = DynamoDbAsyncClient.builder().region(region).build();
        final CloudWatchAsyncClient cloudWatchClient = CloudWatchAsyncClient.builder().region(region).build();

        final ConfigsBuilder configsBuilder = new ConfigsBuilder(streamName, "KinesisProducerLibSampleConsumer", kinesisClient, dynamoClient, cloudWatchClient, UUID.randomUUID().toString(), consumer);

        Executors.newScheduledThreadPool(1).scheduleAtFixedRate(new Runnable() {
            @Override
            public void run() {
                consumer.logResults();
            }
        }, 10, 1, TimeUnit.SECONDS);

        new Scheduler(
                configsBuilder.checkpointConfig(),
                configsBuilder.coordinatorConfig(),
                configsBuilder.leaseManagementConfig(),
                configsBuilder.lifecycleConfig(),
                configsBuilder.metricsConfig(),
                configsBuilder.processorConfig(),
                configsBuilder.retrievalConfig().streamTracker(new SingleStreamTracker(streamName,
                        InitialPositionInStreamExtended.newInitialPosition(InitialPositionInStream.TRIM_HORIZON))))
                .run();
    }
}
