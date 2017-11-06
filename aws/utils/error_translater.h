#pragma once

#include <string>

namespace aws {
  namespace utils {
    std::string translate_last_error();
    std::string translate_error(int code);
    int get_last_error();
  }
}