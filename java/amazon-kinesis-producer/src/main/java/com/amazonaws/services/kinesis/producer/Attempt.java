/*
 * Copyright 2015 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * Licensed under the Amazon Software License (the "License").
 * You may not use this file except in compliance with the License.
 * A copy of the License is located at
 *
 * http://aws.amazon.com/asl/
 *
 * or in the "license" file accompanying this file. This file is distributed
 * on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
 * express or implied. See the License for the specific language governing
 * permissions and limitations under the License.
 */

package com.amazonaws.services.kinesis.producer;

import com.amazonaws.services.kinesis.producer.protobuf.Messages;

/**
 * Represents one attempt at writing a record to the backend. The attempt may or
 * may not be successful. If unsuccessful, an error code and error message are
 * provided. In addition, data about latency are also provided. Each record may
 * have multiple attempts.
 * 
 * @author chaodeng
 * @see UserRecordResult
 */
public class Attempt {
    private int delay;
    private int duration;
    private String errorMessage;
    private String errorCode;
    private boolean success;

    private Attempt(int delay, int duration, String errorMessage, String errorCode, boolean success) {
        this.delay = delay;
        this.duration = duration;
        this.errorMessage = errorMessage;
        this.errorCode = errorCode;
        this.success = success;
    }

    /**
     * 
     * @return Delay in milliseconds between the start of this attempt and the
     *         previous attempt. If this is the first attempt, then returns the
     *         delay between the message reaching the daemon and the first
     *         attempt.
     */
    public int getDelay() {
        return delay;
    }

    /**
     * @return Duration of the attempt. Mainly consists of network and server
     *         latency, but also includes processing overhead within the daemon.
     */
    public int getDuration() {
        return duration;
    }

    /**
     * 
     * @return Error message associated with this attempt. Null if no error
     *         (i.e. successful).
     */
    public String getErrorMessage() {
        return errorMessage;
    }

    /**
     * 
     * @return Error code associated with this attempt. Null if no error
     *         (i.e. successful).
     */
    public String getErrorCode() {
        return errorCode;
    }

    /**
     * 
     * @return Whether the attempt was successful. If true, then the record has
     *         been confirmed by the backend.
     */
    public boolean isSuccessful() {
        return success;
    }
    
    protected static Attempt fromProtobufMessage(Messages.Attempt a) {
        return new Attempt(
                a.getDelay(),
                a.getDuration(),
                a.hasErrorMessage() ? a.getErrorMessage() : null,
                a.hasErrorCode() ? a.getErrorCode() : null,
                a.getSuccess());
    }
}
