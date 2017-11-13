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

#include <aws/core/Aws.h>

struct AwsSdkInitFixture {
  AwsSdkInitFixture() {
    Aws::SDKOptions sdkOptions;
    Aws::InitAPI(sdkOptions);
  }
};

BOOST_GLOBAL_FIXTURE(AwsSdkInitFixture);