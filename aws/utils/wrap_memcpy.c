// Copyright 2017 Amazon.com, Inc. or its affiliates. All Rights Reserved.
//
// Licensed under the Amazon Software License (the "License").
// You may not use this file except in compliance with the License.
// A copy of the License is located at
//
//  http://aws.amazon.com/asl
//
// or in the "license" file accompanying this file. This file is distributed
// on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
// express or implied. See the License for the specific language governing
// permissions and limitations under the License.

#include <string.h>


/**
 * Wrapped version of memcpy provided via asm.
 */
void *__memcpy_2_2_5(void*, const void*, size_t);

asm(".symver __memcpy_2_2_5, memcpy@GLIBC_2.2.5");
/**
 * Wrapped version of memcpy for systems with an older version of glibc.
 *
 * memcpy was updated in glibc 2.14, but the the KPL needs to be able to run on some older versions of Linux.
 * This extracts, and wraps the older version of memcpy, which drops the minimum required version of glibc.
 *
 */
void *__wrap_memcpy(void* dest, const void* src, size_t n) {
  return __memcpy_glibc_2_2_5(dest, src, n);
}
