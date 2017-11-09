cmake_minimum_required(VERSION 3.5)
project(amazon_kinesis_producer)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(TARGET_OS "The OS that the build will target" "")

if(WIN32)
	include("cmake/win32.cmake")
elseif(APPLE)
	include("cmake/macos.cmake")
elseif(UNIX)
	include("cmake/linux.cmake")
elseif(TARGET_OS NOT STREQUAL "")
	include("cmake/${TARGET_OS}")
else()
	message(FATAL_ERROR "Unable to detect OS, you can explicity provide the OS by setting TARGET_OS")
endif()

file(GLOB_RECURSE HEADER_FILES "aws/*.h")

set(SOURCE_FILES        
        aws/kinesis/core/ipc_manager.cc        
        aws/kinesis/core/kinesis_producer.cc        
        aws/kinesis/core/kinesis_record.cc                        
        aws/kinesis/core/retrier.cc
        aws/kinesis/core/shard_map.cc
        aws/kinesis/core/user_record.cc
        aws/metrics/metrics_constants.cc
        aws/metrics/metrics_index.cc
        aws/metrics/metrics_manager.cc
        aws/utils/logging.cc
        aws/utils/utils.cc
        aws/utils/processing_statistics_logger.cc
        aws/utils/wincrypt_md5_hasher.cc
		aws/utils/error_translater.cc
		${PLATFORM_SPECIFIC_SOURCE})

set(TESTS_SOURCE
    aws/utils/test/concurrent_hash_map_test.cc
    aws/utils/test/concurrent_linked_queue_test.cc
    aws/utils/test/spin_lock_test.cc
    aws/utils/test/token_bucket_test.cc
    aws/kinesis/core/test/aggregator_test.cc
    aws/kinesis/core/test/ipc_manager_test.cc
    aws/kinesis/core/test/kinesis_record_test.cc
    aws/kinesis/core/test/limiter_test.cc
    aws/kinesis/core/test/put_records_request_test.cc
    aws/kinesis/core/test/reducer_test.cc
    aws/kinesis/core/test/retrier_test.cc
    aws/kinesis/core/test/shard_map_test.cc
    aws/kinesis/core/test/test_utils.cc
    aws/kinesis/core/test/user_record_test.cc
    aws/metrics/test/accumulator_test.cc
    aws/metrics/test/metric_test.cc
    aws/metrics/test/metrics_manager_test.cc)

include_directories(SYSTEM "${amazon_kinesis_producer_SOURCE_DIR}")

find_package(Threads)



add_executable(kinesis_producer ${SOURCE_FILES} ${HEADER_FILES} aws/kinesis/main.cc ${PROTO_SRCS})
target_link_libraries(kinesis_producer ${THIRD_PARTY_LIBS})

add_executable(test_driver ${SOURCE_FILES} ${HEADER_FILES} aws/kinesis/test_driver.cc ${PROTO_SRCS})
target_link_libraries(test_driver ${THIRD_PARTY_LIBS})

add_executable(tests ${SOURCE_FILES} ${HEADER_FILES} ${TESTS_SOURCE} aws/kinesis/test/test.cc ${PROTO_SRCS})
target_link_libraries(tests ${THIRD_PARTY_LIBS} ${Boost_UNIT_TEST_FRAMEWORK_LIBRARY})

