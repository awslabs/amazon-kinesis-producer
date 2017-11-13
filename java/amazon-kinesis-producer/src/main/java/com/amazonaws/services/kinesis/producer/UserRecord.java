package com.amazonaws.services.kinesis.producer;

import java.nio.ByteBuffer;

public class UserRecord {
    /**
     * Stream to put to.
     */
    private String streamName;

    /**
     * Partition key. Length must be at least one, and at most 256 (inclusive).
     */
    private String partitionKey;

    /**
     * The hash value used to explicitly determine the shard the data
     * record is assigned to by overriding the partition key hash.
     * Must be a valid string representation of a positive integer
     * with value between 0 and <tt>2^128 - 1</tt> (inclusive).
     */
    private String explicitHashKey;

    /**
     * Binary data of the record. Maximum size 1MiB.
     */
    private ByteBuffer data;

    public UserRecord() {
    }

    public UserRecord(String streamName, String partitionKey, ByteBuffer data) {
        this.streamName = streamName;
        this.partitionKey = partitionKey;
        this.data = data;
    }

    public UserRecord(String streamName, String partitionKey, String explicitHashKey, ByteBuffer data) {
        this.streamName = streamName;
        this.partitionKey = partitionKey;
        this.explicitHashKey = explicitHashKey;
        this.data = data;
    }

    public String getStreamName() {
        return streamName;
    }

    public void setStreamName(String streamName) {
        this.streamName = streamName;
    }

    public UserRecord withStreamName(String streamName) {
        this.streamName = streamName;
        return this;
    }

    public String getPartitionKey() {
        return partitionKey;
    }

    public void setPartitionKey(String partitionKey) {
        this.partitionKey = partitionKey;
    }

    public UserRecord withPartitionKey(String partitionKey) {
        this.partitionKey = partitionKey;
        return this;
    }

    public ByteBuffer getData() {
        return data;
    }

    public void setData(ByteBuffer data) {
        this.data = data;
    }

    public UserRecord withData(ByteBuffer byteBuffer) {
        this.data = byteBuffer;
        return this;
    }

    public String getExplicitHashKey() {
        return explicitHashKey;
    }

    public void setExplicitHashKey(String explicitHashKey) {
        this.explicitHashKey = explicitHashKey;
    }

    public UserRecord withExplicitHashKey(String explicitHashKey) {
        this.explicitHashKey = explicitHashKey;
        return this;
    }
}
