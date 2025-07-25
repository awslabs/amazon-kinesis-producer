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

import java.util.ArrayList;
import java.util.List;

import software.amazon.kinesis.producer.protobuf.Messages;
import com.google.common.collect.ImmutableList;

/**
 * The result of a {@link KinesisProducer#addUserRecord} operation. If
 * successful, the shard id and sequence number assigned by the backend are
 * provided. A list of {@link Attempt}s is also provided with details about each
 * attempt made.
 * 
 * @author chaodeng
 * @see Attempt
 */
public class UserRecordResult {
    private List<Attempt> attempts;
    private String sequenceNumber;
    private String shardId;
    private boolean successful;
    private UserRecord userRecord;
    
    public UserRecordResult(List<Attempt> attempts, String sequenceNumber, String shardId, boolean successful) {
        this.attempts = attempts;
        this.sequenceNumber = sequenceNumber;
        this.shardId = shardId;
        this.successful = successful;
    }

    public UserRecordResult(List<Attempt> attempts, String sequenceNumber, String shardId, UserRecord userRecord,
            boolean successful) {
        this.attempts = attempts;
        this.sequenceNumber = sequenceNumber;
        this.shardId = shardId;
        this.userRecord = userRecord;
        this.successful = successful;
    }
    
    /**
     *
     * @return List of {@link Attempt}s, in the order they were made.
     */
    public List<Attempt> getAttempts() {
        return attempts;
    }
    
    /**
     * 
     * @return The sequence number assigned by the backend to this record.
     *         Multiple records may have the same sequence number if aggregation
     *         is enabled. Will be null if the put failed.
     */
    public String getSequenceNumber() {
        return sequenceNumber;
    }
    
    /**
     * 
     * @return Shard ID returned by the backend. The record was written to this
     *         shard. Will be null if the put failed.
     */
    public String getShardId() {
        return shardId;
    }
    
    /**
     * 
     * @return Whether the record put was successful. If true, then the record
     *         has been confirmed by the backend.
     */
    public boolean isSuccessful() {
        return successful;
    }

    /**
     *
     * @return The UserRecord that was attempted to be put
     */
    public UserRecord getUserRecord() {
        return userRecord;
    }
    
    protected static UserRecordResult fromProtobufMessage(Messages.PutRecordResult r) {
        final List<Attempt> attempts = new ArrayList<>(r.getAttemptsCount());
        for (Messages.Attempt a : r.getAttemptsList()) {
            attempts.add(Attempt.fromProtobufMessage(a));
        }
        return new UserRecordResult(
                new ImmutableList.Builder<Attempt>().addAll(attempts).build(),
                r.hasSequenceNumber() ? r.getSequenceNumber() : null,
                r.hasShardId() ? r.getShardId() : null,
                r.hasData() ? new UserRecord(r.getStreamName(), r.getPartitionKey(), r.getExplicitHashKey(),
                        r.getData().asReadOnlyByteBuffer()) : null,
                r.getSuccess());
    }
}
