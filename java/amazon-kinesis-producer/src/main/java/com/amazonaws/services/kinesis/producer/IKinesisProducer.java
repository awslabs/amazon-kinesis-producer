package com.amazonaws.services.kinesis.producer;

import java.nio.ByteBuffer;
import java.util.List;
import java.util.concurrent.ExecutionException;
import com.google.common.util.concurrent.ListenableFuture;


public interface IKinesisProducer {
    ListenableFuture<UserRecordResult> addUserRecord(String stream, String partitionKey, ByteBuffer data);

    ListenableFuture<UserRecordResult> addUserRecord(UserRecord userRecord);

    ListenableFuture<UserRecordResult> addUserRecord(String stream, String partitionKey, String explicitHashKey, ByteBuffer data);

    int getOutstandingRecordsCount();

    long getOldestRecordTimeInSeconds();

    List<Metric> getMetrics(String metricName, int windowSeconds) throws InterruptedException, ExecutionException;

    List<Metric> getMetrics(String metricName) throws InterruptedException, ExecutionException;

    List<Metric> getMetrics() throws InterruptedException, ExecutionException;

    List<Metric> getMetrics(int windowSeconds) throws InterruptedException, ExecutionException;

    void destroy();

    void flush(String stream);

    void flush();

    void flushSync();
}
