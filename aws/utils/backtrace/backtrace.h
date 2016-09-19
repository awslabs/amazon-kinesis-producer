#ifndef VARIED_BACKTRACE_H
#define VARIED_BACKTRACE_H

namespace aws {
namespace utils {
namespace backtrace {

void initialize(const char* exe);
void stack_trace_for_signal(int skip = 0, bool signaled = true);

}
}
}

#endif // VARIED_BACKTRACE_H
