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

/**
 * The exception thrown when an unexpected message is read from the native layer.
 */
public class UnexpectedMessageException extends KinesisProducerException {
    private static final long serialVersionUID = 3168271192277927600L;

    public UnexpectedMessageException(String message) {
        super(message);
    }

    public UnexpectedMessageException(String message, UserRecord userRecord) {
        super(message, userRecord);
    }

}

