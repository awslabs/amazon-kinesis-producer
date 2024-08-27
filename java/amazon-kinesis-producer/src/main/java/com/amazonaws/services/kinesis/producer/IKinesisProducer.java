package com.amazonaws.services.kinesis.producer;

import java.nio.ByteBuffer;
import java.util.List;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.ExecutionException;
import com.amazonaws.services.schemaregistry.common.Schema;


public interface IKinesisProducer {
    CompletableFuture<UserRecordResult> addUserRecord(String stream, String partitionKey, ByteBuffer data);

    CompletableFuture<UserRecordResult> addUserRecord(UserRecord userRecord);

    CompletableFuture<UserRecordResult> addUserRecord(String stream, String partitionKey, String explicitHashKey, ByteBuffer data);

    CompletableFuture<UserRecordResult> addUserRecord(String stream, String partitionKey, String explicitHashKey, ByteBuffer data, Schema schema);

    int getOutstandingRecordsCount();

    default long getOldestRecordTimeInMillis() {
        throw new UnsupportedOperationException("This method is not supported in this IKinesisProducer type");
    }

    List<Metric> getMetrics(String metricName, int windowSeconds) throws InterruptedException, ExecutionException;

    List<Metric> getMetrics(String metricName) throws InterruptedException, ExecutionException;

    List<Metric> getMetrics() throws InterruptedException, ExecutionException;

    List<Metric> getMetrics(int windowSeconds) throws InterruptedException, ExecutionException;

    void destroy();

    void flush(String stream);

    void flush();

    void flushSync();
}
