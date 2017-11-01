#pragma once

#include <string>
#include <array>
#include <cstdint>

namespace aws {
  namespace utils {
    namespace MD5 {
      const size_t kMD5ByteLength = 16;
      void initialize();
      std::array<std::uint8_t, kMD5ByteLength> hash(const std::string& data);
    }
  }
}