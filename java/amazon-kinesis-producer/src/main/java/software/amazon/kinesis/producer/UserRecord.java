package software.amazon.kinesis.producer;

import com.amazonaws.services.schemaregistry.common.Schema;

import java.nio.ByteBuffer;

import lombok.ToString;

@ToString
public class UserRecord {
    /**
     * Stream to put to.
     */
    private String streamName;

    /**
     * ARN of the stream, e.g., arn:aws:kinesis:us-east-2:123456789012:stream/mystream
     */
    private String streamARN;

    /**
     * Partition key. Length must be at least one, and at most 256 (inclusive).
     */
    private String partitionKey;

    /**
     * The hash value used to explicitly determine the shard the data
     * record is assigned to by overriding the partition key hash.
     * Must be a valid string representation of a positive integer
     * with value between 0 and <code>2^128 - 1</code> (inclusive).
     */
    private String explicitHashKey;

    /**
     * Binary data of the record. Maximum size 1MiB.
     */
    private ByteBuffer data;

    /**
     * Specify Schema for the data. Schemas are administered by AWS Glue Schema Registry.
     * Read Glue Schema Registry docs on how to get started on using Schema for your data.
     * This is an optional field.
     */
    private Schema schema;

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

    public UserRecord(String streamName, String partitionKey, String explicitHashKey, ByteBuffer data, Schema schema) {
        this.streamName = streamName;
        this.partitionKey = partitionKey;
        this.explicitHashKey = explicitHashKey;
        this.data = data;
        this.schema = schema;
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

    public Schema getSchema() {
        return schema;
    }

    public void setSchema(Schema schema) {
        this.schema = schema;
    }

    public UserRecord withSchema(Schema schema) {
        this.schema = schema;
        return this;
    }
}
