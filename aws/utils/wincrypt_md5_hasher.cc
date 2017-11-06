#include "md5_hasher.h"
#include <Windows.h>
#include <wincrypt.h>
#include <sstream>
#include <algorithm>
#include <array>

using namespace aws::utils::MD5;

namespace {
  HCRYPTPROV hCryptoProvider = NULL;

  std::string FormatError(const std::string& context, DWORD lastError) {
    LPSTR buffer = NULL;
    std::stringstream ss;
    ss << "Error while " << context << ". Error (" << lastError << "): ";
    if (FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM, NULL, lastError, 0, buffer, 0, NULL)) {
      ss << buffer;
      LocalFree(buffer);
    }
    else {
      ss << "Failed to decode error due to " << GetLastError() << " while attempting to decode error message";
    }
    return ss.str();
  }

  struct HashHolder {
    HCRYPTHASH hHash = NULL;

    ~HashHolder() {
      if (hHash) {
        //
        // Ignore the return result, as there really isn't any recovery at this point.
        //
        CryptDestroyHash(hHash);
      }
    }
  };

  
}

void aws::utils::MD5::initialize() {
  if (hCryptoProvider) {
    //
    // Already initialized
    //
    return;
  }
  if (!CryptAcquireContext(&hCryptoProvider, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) {
    //
    // Failed to create a context
    //		
    std::string errorMessage = FormatError("Initializing Crypto Provider", GetLastError());
    throw std::runtime_error(errorMessage);
  }
}

std::array<uint8_t, kMD5ByteLength> aws::utils::MD5::hash(const std::string& data) {
  if (!hCryptoProvider) {
    throw std::runtime_error("Crypto provider wasn't initialized.  Applications must call aws::utils::MD5::initialize() first.");
  }
  HashHolder hash;
  if (!CryptCreateHash(hCryptoProvider, CALG_MD5, 0, 0, &hash.hHash)) {
    std::string errorMessage = FormatError("Creating Hash", GetLastError());
    throw std::runtime_error(errorMessage);
  }
  const char* rawData = data.c_str();
  const BYTE* bytes = reinterpret_cast<const BYTE*>(rawData);
  if (!CryptHashData(hash.hHash, bytes, data.length(), 0)) {
    std::string errorMesage = FormatError("Hashing Data", GetLastError());
    throw std::runtime_error(errorMesage);
  }


  DWORD hashDataLen = kMD5ByteLength;
  std::array<BYTE, kMD5ByteLength> hashData;
  if (!CryptGetHashParam(hash.hHash, HP_HASHVAL, hashData.data(), &hashDataLen, 0)) {
    std::string errorMessage = FormatError("Retrieving Sized of Hashed Data", GetLastError());
    throw std::runtime_error(errorMessage);
  }

  std::array<uint8_t, kMD5ByteLength> result = hashData;

  //return reinterpret_cast<std::array<uint8_t, kMD5ByteLength>>(hashData);

  return result;
}

