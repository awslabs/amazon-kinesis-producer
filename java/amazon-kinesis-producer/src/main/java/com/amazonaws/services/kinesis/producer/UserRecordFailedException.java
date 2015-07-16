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

public class UserRecordFailedException extends Exception {
    private static final long serialVersionUID = 3168271192277927600L;

    private UserRecordResult result;
    
    public UserRecordFailedException(UserRecordResult result) {
        this.result = result;
    }

    public UserRecordResult getResult() {
        return result;
    }
}
