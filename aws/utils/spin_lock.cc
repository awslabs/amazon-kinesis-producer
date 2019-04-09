/*
 * Copyright 2019 Amazon.com, Inc. or its affiliates.
 * Licensed under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

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
