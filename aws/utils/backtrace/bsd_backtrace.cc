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


