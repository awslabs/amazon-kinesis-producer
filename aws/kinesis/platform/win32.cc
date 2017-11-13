#include "platform.h"
#include <Windows.h>

namespace aws {
  namespace kinesis {
    namespace platform {
      void initialize() {
        //
        // Disable the Windows Error Reporting Dialog during shutdown.  This occurs due to shutdown not actually being handled correctly.
        // TODO: Fix shutdown to not require this.
        //
        UINT currentErrorMode = GetErrorMode();
        SetErrorMode(currentErrorMode | SEM_NOGPFAULTERRORBOX);
      }
    }
  }
}