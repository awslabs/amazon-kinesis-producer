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

#include "backtrace.h"
#include <aws/utils/writer_methods.h>
#include <execinfo.h>
#include <unistd.h>
#include <aws/utils/logging.h>
#include <aws/utils/writer_methods.h>
#include <cstring>
#include <cstdio>

#ifdef LIB_BACKTRACE

#include <backtrace.h>

namespace {
    backtrace_state* backtrace_state_ = nullptr;

    bool is_initializing_ = true;

    void error_callback(void* data, const char* msg, int errnum) {
        if (is_initializing_) {
            LOG(error) << "Failed to configure the backtrace library due to '" << msg << "': " << errnum;
        } else {
            WRITE_MESSAGE("Error while producing stacktrace (");
            WRITE_NUM_CHECKED(errnum, "NEG");
            WRITE_MESSAGE(")");
            aws::utils::writer::write_message(msg, strlen(msg) - 1);
        }
    }

    void syminfo_callback(void* data, uintptr_t pc, const char* symname, uintptr_t symval, uintptr_t symsize) {
        if (data && !*(bool*)data) {
            if (symname) {
                printf("%p %s ??:0\n", (unsigned long)pc, symname);
            } else {
                printf("%p ?? ??:0\n", (unsigned long)pc);
            }
        } else {
            if (symname) {
                WRITE_NUMBER(pc);
                WRITE_MESSAGE(" ");
                aws::utils::writer::write_message(symname, strlen(symname));
                WRITE_MESSAGE(":0 ??");
            } else {
                WRITE_NUMBER(pc);
                WRITE_MESSAGE("??:0 ??");
            }
            WRITE_MESSAGE("\n");
        }

    }

    int full_callback(void* data, uintptr_t pc, const char* filename, int lineno, const char* function) {
        if (data && !*(bool*)(data)) {
            if (function) {
                printf("%p %s:%d %s(...)\n", pc, filename ? filename : "??", lineno, function);
            } else {
                backtrace_syminfo(backtrace_state_, pc, &syminfo_callback, &error_callback, data);
            }
        }
        WRITE_NUMBER(pc);
        WRITE_MESSAGE(" ");
        if (function) {
            if (filename) {
                aws::utils::writer::write_message(filename, strlen(filename));
            } else {
                WRITE_MESSAGE("Unknown File");
            }
            if (lineno) {
                WRITE_MESSAGE(":");
                WRITE_NUM_CHECKED(lineno, "INV");
            }
            if (function) {
                WRITE_MESSAGE(" ");
                aws::utils::writer::write_message(function, strlen(function));
                WRITE_MESSAGE("(...)");
            }
            WRITE_MESSAGE("\n");
        } else {
            backtrace_syminfo(backtrace_state_, pc, &syminfo_callback, &error_callback, data);
        }


        return 0;
    }
}

namespace aws {
namespace utils {
namespace backtrace {

void initialize(const char* exe) {
    backtrace_state_ = backtrace_create_state(exe, 1, &error_callback, nullptr);
    if (backtrace_state_ == nullptr) {
        LOG(error) << "Failed to initialize backtrace reporting.";
    }
    is_initializing_ = false;
}

void stack_trace_for_signal(int skip, bool signaled) {
   if (!backtrace_state_) {
       WRITE_MESSAGE("Setup of backtrace failed.  Not emitting backtrace.");
       return;
   }
   int result = backtrace_full(backtrace_state_, skip, &full_callback, &error_callback, &signaled);
   if (result != 0) {
       WRITE_MESSAGE("Error encountered while writing backtrace.");
   }
}

}
}
}

#endif //LIB_BACKTRACE
