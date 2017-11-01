// Copyright 2015 Amazon.com, Inc. or its affiliates. All Rights Reserved.
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

#define BOOST_TEST_MAIN
#include <boost/test/unit_test.hpp>

#ifdef WIN32
#include "aws/utils/md5_hasher.h"
#endif



struct HashInitFixture {
  HashInitFixture() {
#ifdef WIN32
    aws::utils::MD5::initialize();
#endif
  }
};

BOOST_TEST_GLOBAL_FIXTURE(HashInitFixture);