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

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.concurrent.Executors;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicLong;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.amazonaws.auth.DefaultAWSCredentialsProviderChain;
import com.amazonaws.services.kinesis.clientlibrary.interfaces.IRecordProcessor;
import com.amazonaws.services.kinesis.clientlibrary.interfaces.IRecordProcessorCheckpointer;
import com.amazonaws.services.kinesis.clientlibrary.interfaces.IRecordProcessorFactory;
import com.amazonaws.services.kinesis.clientlibrary.lib.worker.InitialPositionInStream;
import com.amazonaws.services.kinesis.clientlibrary.lib.worker.KinesisClientLibConfiguration;
import com.amazonaws.services.kinesis.clientlibrary.lib.worker.Worker;
import com.amazonaws.services.kinesis.clientlibrary.lib.worker.ShutdownReason;
import com.amazonaws.services.kinesis.model.Record;

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
 * @see SampleProducer
 * @author chaodeng
 *
 */
public class SampleConsumer implements IRecordProcessorFactory {
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

    /**
     * One instance of RecordProcessor is created for every shard in the stream.
     * All instances of RecordProcessor share state by capturing variables from
     * the enclosing SampleConsumer instance. This is a simple way to combine
     * the data from multiple shards.
     */
    private class RecordProcessor implements IRecordProcessor {
        @Override
        public void initialize(String shardId) {}

        @Override
        public void processRecords(List<Record> records, IRecordProcessorCheckpointer checkpointer) {
            long timestamp = 0;
            List<Long> seqNos = new ArrayList<>();
            
            for (Record r : records) {
                // Get the timestamp of this run from the partition key.
                timestamp = Math.max(timestamp, Long.parseLong(r.getPartitionKey()));
                
                // Extract the sequence number. It's encoded as a decimal
                // string and placed at the beginning of the record data,
                // followed by a space. The rest of the record data is padding
                // that we will simply discard.
                try {
                    byte[] b = new byte[r.getData().remaining()];
                    r.getData().get(b);
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
                checkpointer.checkpoint();
            } catch (Exception e) {
                log.error("Error while trying to checkpoint during ProcessRecords", e);
            }
        }

        @Override
        public void shutdown(IRecordProcessorCheckpointer checkpointer, ShutdownReason reason) {
            log.info("Shutting down, reason: " + reason);
            try {
                checkpointer.checkpoint();
            } catch (Exception e) {
                log.error("Error while trying to checkpoint during Shutdown", e);
            }
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
    
    @Override
    public IRecordProcessor createProcessor() {
        return this.new RecordProcessor();
    }
    
    public static void main(String[] args) {
        KinesisClientLibConfiguration config =
                new KinesisClientLibConfiguration(
                        "KinesisProducerLibSampleConsumer",
                        SampleProducer.STREAM_NAME,
                        new DefaultAWSCredentialsProviderChain(),
                        "KinesisProducerLibSampleConsumer")
                                .withRegionName(SampleProducer.REGION)
                                .withInitialPositionInStream(InitialPositionInStream.TRIM_HORIZON);
        
        final SampleConsumer consumer = new SampleConsumer();
        
        Executors.newScheduledThreadPool(1).scheduleAtFixedRate(new Runnable() {
            @Override
            public void run() {
                consumer.logResults();
            }
        }, 10, 1, TimeUnit.SECONDS);
        
        new Worker.Builder()
            .recordProcessorFactory(consumer)
            .config(config)
            .build()
            .run();
    }
}
