#include "error_translater.h"

#include <sstream>

#include <Windows.h>
namespace aws {
  namespace utils {
    int get_last_error() {
      return GetLastError();
    }

    std::string translate_error(int code) {
      std::stringstream ss;
      LPSTR buffer = NULL;
      if (FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM, NULL, code, 0, (LPSTR)&buffer, 0, NULL)) {
        ss << buffer;
        LocalFree(buffer);
      }
      else {
        ss << "Failed to translate error (" << code << ") due to " << GetLastError() << " while attempting to translate the error code";
      }
      return ss.str();
    }

    std::string translate_last_error() {
      return translate_error(get_last_error());
    }
  }
}