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
