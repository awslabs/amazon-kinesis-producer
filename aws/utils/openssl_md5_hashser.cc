#include "md5_hasher.h"

#include <openssl/md5.h>

namespace {
  template <typename Func, typename ...Params>
  void checked_invoke(Func f, Params&&... params) {
    if (f(std::forward<Params>(params)...) != 1) {
      throw std::runtime_error("C function's return value indicated an error");
    }
  }
}

namespace aws {
  namespace utils {
    namespace MD5 {
      void initialize() {

      }

      std::array<std::uint8_t, kMD5ByteLength> hash(const std::string& data) {
        MD5_CTX ctx;
        checked_invoke(&MD5_Init, &ctx);
        checked_invoke(&MD5_Update, &ctx, data.data(), data.length());
        std::array<uint8_t, MD5_DIGEST_LENGTH> buf;
        checked_invoke(&MD5_Final, (unsigned char*)buf.data(), &ctx);
        return buf;
      }
    }
  }
}