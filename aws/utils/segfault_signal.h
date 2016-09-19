#ifndef SEGFAULT_SIGNAL_H
#define SEGFAULT_SIGNAL_H

#ifdef SEGFAULT_SIGNAL_ENABLED

#define CHECK_SEGFAULT_STATUS() ::aws::utils::trigger_segfault_if_needed();
#define INSTALL_SEGFAULT_TRIGGER() ::aws::utils::install_user_handler();

#include <aws/utils/signal_handler.h>

namespace aws {
namespace utils {

void trigger_segfault_if_needed();
void install_user_handler();

}
}
#else
#define CHECK_SEGFAULT_STATUS()
#define INSTALL_SEGFAULT_TRIGGER()
#endif

#endif // SEGFAULT_SIGNAL_H
