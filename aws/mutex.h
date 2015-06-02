// Copyright 2010-2014 Amazon.com, Inc. or its affiliates. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License").
// You may not use this file except in compliance with the License.
// A copy of the License is located at
//
//  http://aws.amazon.com/apache2.0
//
// or in the "license" file accompanying this file. This file is distributed
// on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
// express or implied. See the License for the specific language governing
// permissions and limitations under the License.

#ifndef AWS_MUTEX_H_
#define AWS_MUTEX_H_

#include <boost/predef.h>

#if BOOST_OS_WINDOWS == 0
  #include <shared_mutex>
  #include <condition_variable>
  #include <thread>
#endif

#include <boost/thread.hpp>
#include <boost/thread/locks.hpp>
#include <boost/thread/lock_types.hpp>
#include <boost/thread/lock_options.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/thread/shared_mutex.hpp>
#include <boost/thread/recursive_mutex.hpp>
#include <boost/thread/condition_variable.hpp>

// Hopefully the need for this file goes away eventually.
// We don't just always the boost versions because the LLVM implementations show
// better performance.
namespace aws {

#if BOOST_OS_WINDOWS

namespace threading_namespace = boost;

using shared_mutex = threading_namespace::shared_mutex;

#else

namespace threading_namespace = std;

using shared_mutex = threading_namespace::shared_timed_mutex;

#endif

namespace this_thread = threading_namespace::this_thread;

using thread = threading_namespace::thread;
using condition_variable = threading_namespace::condition_variable;
using mutex = threading_namespace::mutex;
using recursive_mutex = threading_namespace::recursive_mutex;

template <typename Mutex>
using unique_lock = threading_namespace::unique_lock<Mutex>;

template <typename Mutex>
using shared_lock = threading_namespace::shared_lock<Mutex>;

template <typename Mutex>
using lock_guard = threading_namespace::lock_guard<Mutex>;

static const threading_namespace::defer_lock_t defer_lock{};

} //namespace aws

#endif //AWS_MUTEX_H_
