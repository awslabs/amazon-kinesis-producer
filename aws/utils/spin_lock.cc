// Copyright 2018 Amazon.com, Inc. or its affiliates. All Rights Reserved.
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

#include "spin_lock.h"

using namespace aws::utils;
#ifdef DEBUG
namespace {
  thread_local TicketSpinLock::DebugStats debug_stats;
}

void TicketSpinLock::add_acquired() {
  debug_stats.acquired_count++;
}

void TicketSpinLock::add_acquired_lock() {
  debug_stats.acquired_with_lock++;
}

void TicketSpinLock::add_spin() {
  debug_stats.total_spins++;
}

TicketSpinLock::DebugStats TicketSpinLock::get_debug_stats() {
  return debug_stats;
}
#endif
