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

#ifndef VARIED_BACKTRACE_H
#define VARIED_BACKTRACE_H

namespace aws {
namespace utils {
namespace backtrace {

void initialize(const char* exe);
void stack_trace_for_signal(int skip = 0, bool signaled = true);

}
}
}

#endif // VARIED_BACKTRACE_H
