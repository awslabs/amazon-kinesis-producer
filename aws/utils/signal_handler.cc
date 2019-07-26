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

#include "signal_handler.h"
#include "segfault_signal.h"
#include "writer_methods.h"
#include "backtrace/backtrace.h"

#include <signal.h>
#include <execinfo.h>
#include <unistd.h>
#include <cstring>
#include <cstdint>
#include <cstdlib>

static size_t signal_message_sizes[NSIG];

void write_signal_description(int signal) {
    if (signal <= 0 || signal >= NSIG) {
        WRITE_MESSAGE("Can't Find Signal Description for ")
        WRITE_NUM_CHECKED(signal, "Negative Signal")
    } else {
        if (signal_message_sizes[signal]) {
            write(STDERR_FILENO, sys_siglist[signal], signal_message_sizes[signal]);
        } else {
            WRITE_MESSAGE("Signal had NULL message.")
        }
    }
}

static void signal_handler(int, siginfo_t *info, void *) {
    WRITE_MESSAGE("\n++++\n");
    if (info->si_signo == SIGUSR1) {
        WRITE_MESSAGE("[INFO]\n");
        WRITE_MESSAGE("User Requested Stack Trace\n");
        WRITE_MESSAGE("---BEGIN INFO---\n");
    } else {
        WRITE_MESSAGE("[ERROR]\n");
        WRITE_MESSAGE("---BEGIN ERROR----\n");
    }
    WRITE_MESSAGE("Signal: ");

    switch (info->si_signo) {
    case SIGQUIT:
        WRITE_MESSAGE("SIGQUIT")
            break;
    case SIGILL:
        WRITE_MESSAGE("SIGILL: ");
        WRITE_MESSAGE(" Code: (");
        WRITE_CODE(info->si_code);
        WRITE_MESSAGE(") Address: ");
        WRITE_POINTER(info->si_addr);
        break;
    case SIGBUS:
        WRITE_MESSAGE("SIGBUS");
        WRITE_MESSAGE(" Code: (");
        WRITE_CODE(info->si_code);
        WRITE_MESSAGE(") Address: ");
        WRITE_POINTER(info->si_addr);
        break;
    case SIGSEGV:
        WRITE_MESSAGE("SIGSEGV");
        WRITE_MESSAGE(" Code: (");
        WRITE_CODE(info->si_code);
        WRITE_MESSAGE(") Address: ");
        WRITE_POINTER(info->si_addr);
        break;
    case SIGUSR1:
        WRITE_MESSAGE("SIGUSR1");
        break;
    default:
        WRITE_MESSAGE("Unhandled Signal(");
        WRITE_NUM_CHECKED(info->si_signo, "Negative Signal?");
        WRITE_MESSAGE(")");
        break;
    }
    WRITE_MESSAGE("\n");
    WRITE_MESSAGE("Description: ");
    write_signal_description(info->si_signo);
    WRITE_MESSAGE("\n");
    WRITE_MESSAGE("---BEGIN STACK TRACE---\n");
    aws::utils::backtrace::stack_trace_for_signal();
    WRITE_MESSAGE("---END STACK TRACE---\n");
    if (info->si_signo == SIGUSR1) {
        WRITE_MESSAGE("---END INFO---\n");
    } else {
        WRITE_MESSAGE("---END ERROR---\n");
    }
    WRITE_MESSAGE("----\n");
    

    if (info->si_signo != SIGUSR1) {
        abort();
    }

}


namespace aws {
    namespace utils {
        void setup_stack_trace(const char* exe) {
            aws::utils::backtrace::initialize(exe);
            signal_message_sizes[0] = 0;
            for (int i = 1; i < NSIG; ++i) {
                if (sys_siglist[i]) {
                    signal_message_sizes[i] = strlen(sys_siglist[i]);
                }
            }
            sigset_t mask;
            sigemptyset(&mask);
            sigaddset(&mask, SIGQUIT);
            sigaddset(&mask, SIGILL);
            sigaddset(&mask, SIGBUS);
            sigaddset(&mask, SIGSEGV);
            sigaddset(&mask, SIGUSR1);

            struct sigaction action;
            action.sa_sigaction = &signal_handler;
            action.sa_flags = SA_SIGINFO;
            action.sa_mask = mask;

            sigaction(SIGQUIT, &action, NULL);
            sigaction(SIGILL, &action, NULL);
            sigaction(SIGBUS, &action, NULL);
            sigaction(SIGSEGV, &action, NULL);

            //
            // This enables customers to trigger a stack trace at any time by sending SIGUSR1 to the Kinesis Producer
            // PID.  Unfortunately there is no way to control which thread actually handles the signal, so this is of
            // questionable utility.
            //
            // TODO: Change the overall thread handling to allow triggering a stack trace from every thread.
            //
            sigaction(SIGUSR1, &action, NULL);

            //
            // Ignoring this since curl/OpenSSL can trigger them and something is wrong with it's ignore
            //
            struct sigaction pipe_action = {};
            pipe_action.sa_handler = SIG_IGN;
            sigaction(SIGPIPE, &pipe_action, NULL);
        }

        void enable_segfault_trigger() {

        }
    }
}
