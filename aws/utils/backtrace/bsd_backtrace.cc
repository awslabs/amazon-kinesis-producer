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

#ifdef BSD_BACKTRACE
namespace aws {
namespace utils {
namespace backtrace {

void initialize(const char* exe) {

}

void stack_trace_for_signal(int skip, bool /*signaled*/) {
    void *buffer[100];
    int symbol_count = ::backtrace(buffer, sizeof(buffer));
    WRITE_MESSAGE("SYMBOLS:(")
    WRITE_NUM_CHECKED(symbol_count, "NEG")
    WRITE_MESSAGE("/")
    WRITE_NUMBER(sizeof(buffer))
    WRITE_MESSAGE(")\n")
    if (symbol_count > 0) {
        backtrace_symbols_fd(buffer, symbol_count, STDERR_FILENO);
    }
    WRITE_MESSAGE("\n")
}

}
}
}
#endif // BSD_STACKTRACE


