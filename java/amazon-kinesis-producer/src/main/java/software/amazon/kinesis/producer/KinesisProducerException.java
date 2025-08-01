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

import lombok.Getter;
/**
 * This is the wrapper exception thrown and returned to callbacks in onFailure. If
 * {@link KinesisProducerConfiguration#setReturnUserRecordInFuture(boolean)} is set to true, this will contain
 * the userRecord associated with the future returned from a {@link KinesisProducer#addUserRecord(UserRecord)}
 * call or any of its overloaded methods.
 */
public class KinesisProducerException extends Exception {
    private static final long serialVersionUID = 3168271192277927600L;

    /**
     * If {@link KinesisProducerConfiguration#setReturnUserRecordInFuture(boolean)} is set to true,
     * this will contain UserRecord associated with this future. If it is false, this will be null.
     */
    @Getter
    private final UserRecord userRecord;

    public KinesisProducerException(Throwable cause, UserRecord userRecord) {
        super(cause);
        this.userRecord = userRecord;
    }

    public KinesisProducerException(String message, UserRecord userRecord) {
        super(message);
        this.userRecord = userRecord;
    }

    public KinesisProducerException(String message) {
        super(message);
        this.userRecord = null;
    }

    public KinesisProducerException(UserRecord userRecord) {
        this.userRecord = userRecord;
    }

}
