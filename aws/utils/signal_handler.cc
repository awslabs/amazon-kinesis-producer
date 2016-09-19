//
// Created by Pfifer, Justin on 4/29/16.
//

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
#include <thread>
#include <mutex>
#include <atomic>

static size_t signal_message_sizes[NSIG];

void write_signal_descriptiong(int signal) {
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

static std::mutex display_mutex;

static void signal_handler(int, siginfo_t *info, void *) {
    {
        std::lock_guard<std::mutex> lock(display_mutex);                
        WRITE_MESSAGE("\n++++\n")
        WRITE_MESSAGE("[ERROR]\n")        
        WRITE_MESSAGE("---BEGIN ERROR----\n");
        WRITE_MESSAGE("Signal: ");

        switch (info->si_signo) {
            case SIGQUIT:
                WRITE_MESSAGE("SIGQUIT")
                break;
            case SIGILL:
                WRITE_MESSAGE("SIGILL: ")
                WRITE_MESSAGE(" Code: (")
                WRITE_CODE(info->si_code)
                WRITE_MESSAGE(") Address: ")
                WRITE_POINTER(info->si_addr)
                break;
            case SIGBUS:
                WRITE_MESSAGE("SIGBUS")
                WRITE_MESSAGE(" Code: (")
                WRITE_CODE(info->si_code)
                WRITE_MESSAGE(") Address: ")
                WRITE_POINTER(info->si_addr)
                break;
            case SIGSEGV:
                WRITE_MESSAGE("SIGSEGV")
                WRITE_MESSAGE(" Code: (")
                WRITE_CODE(info->si_code)
                WRITE_MESSAGE(") Address: ")
                WRITE_POINTER(info->si_addr)
                break;
            case SIGPIPE:
                WRITE_MESSAGE("SIGPIPE")
                break;
            default:
                WRITE_MESSAGE("Unhandled Signal(")
                WRITE_NUM_CHECKED(info->si_signo, "Negative Signal?")
                WRITE_MESSAGE(")")
                break;
        }
        WRITE_MESSAGE("\n");
        WRITE_MESSAGE("Description: ")
        write_signal_descriptiong(info->si_signo);
        WRITE_MESSAGE("\n")
        WRITE_MESSAGE("---BEGIN STACK TRACE---\n")
        aws::utils::backtrace::stack_trace_for_signal();
        WRITE_MESSAGE("---END STACK TRACE---\n")
        WRITE_MESSAGE("---END ERROR---\n")
        WRITE_MESSAGE("----\n");
    }

    if (info->si_signo != SIGPIPE) {
        sleep(2);
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
            struct sigaction action;
            action.sa_sigaction = &signal_handler;
            action.sa_flags |= SA_SIGINFO;

            sigaction(SIGQUIT, &action, NULL);
            sigaction(SIGILL, &action, NULL);
            //
            // Disabling for now, since the handler will actually trigger an abort once it's done.
            //
            // sigaction(SIGABRT, &action, NULL);
            sigaction(SIGBUS, &action, NULL);
            sigaction(SIGSEGV, &action, NULL);
            //
            // Disabling this since curl can trigger them and doesn't ignore them
            //
            sigaction(SIGPIPE, &action, NULL);
        }

        void enable_segfault_trigger() {

        }
    }
}
