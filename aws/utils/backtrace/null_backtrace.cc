// Copyright 2015 Amazon.com, Inc. or its affiliates. All Rights Reserved.
//
// Licensed under the Amazon Software License (the "License").
// You may not use this file except in compliance with the License.
// A copy of the License is located at
//
//  http://aws.amazon.com/asl
//
// or in the "license" file accompanying this file. This file is distributed
// on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
// express or implied. See the License for the specific language governing
// permissions and limitations under the License.

#include "backtrace.h"
#include <aws/utils/writer_methods.h>
#include <execinfo.h>
#include <unistd.h>

#ifdef NULL_STACKTRACE

namespace aws {
namespace utils {
namespace backtrace {

void initialize(const char* exe) {

}

void stack_trace_for_signal(int skip, bool /*signaled*/) {
    WRITE_MESSAGE("Backtrace not supported.");
}

}
}
}

#endif // NULL_STACKTRACE
