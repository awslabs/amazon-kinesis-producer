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

public class IrrecoverableError extends RuntimeException {
    private static final long serialVersionUID = 2657526423645068574L;

    public IrrecoverableError(String message) {
        super(message);
    }
    
    public IrrecoverableError(Throwable t) {
        super(t);
    }
    
    public IrrecoverableError(String message, Throwable t) {
        super(message, t);
    }
}
