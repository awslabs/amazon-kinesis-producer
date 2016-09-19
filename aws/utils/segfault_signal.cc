#include "segfault_signal.h"
#include <signal.h>
#include <atomic>
#include <aws/utils/logging.h>

#ifdef SEGFAULT_SIGNAL_ENABLED
namespace {

std::atomic<bool> segfault_signaled_(false);

void user_signal_handler(int, siginfo_t *info, void *) {
    segfault_signaled_.store(true);
}

volatile int* p = (int*)0;

}

namespace aws {
namespace utils {
void trigger_segfault_if_needed() {
    if (segfault_signaled_.load()) {
        LOG(info) << "Triggering segfault due to signal.";
        *p = 10;
    }
}

void install_user_handler() {
    struct sigaction action;
    action.sa_sigaction = &user_signal_handler;
    action.sa_flags |= SA_SIGINFO;

    sigaction(SIGUSR1, &action, NULL);
}

}
}
#endif
