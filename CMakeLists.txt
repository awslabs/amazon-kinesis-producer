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
		aws/utils/interlock.cc
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
    aws/kinesis/core/test/test_utils.h
    aws/kinesis/core/test/user_record_test.cc
    aws/metrics/test/accumulator_test.cc
    aws/metrics/test/metric_test.cc
    aws/metrics/test/metrics_manager_test.cc
    )

include_directories(SYSTEM "${amazon_kinesis_producer_SOURCE_DIR}")

find_package(aws-sdk-cpp REQUIRED)

# set(Boost_USE_STATIC_LIBS ON)
find_package(Boost REQUIRED COMPONENTS regex thread log system random filesystem chrono program_options unit_test_framework)
find_package(Protobuf REQUIRED)
find_package(Threads)

set(DEFAULT_BOOST_LIBS
	${Boost_REGEX_LIBRARY}
	${Boost_THREAD_LIBRARY}
	${Boost_LOG_LIBRARY}
	${Boost_SYSTEM_LIBRARY}
	${Boost_RANDOM_LIBRARY}
	${Boost_FILESYSTEM_LIBRARY}
	${Boost_CHRONO_LIBRARY}
	${Boost_DATE_TIME_LIBRARY}
	${Boost_ATOMIC_LIBRARY}
	${Boost_LOG_SETUP_LIBRARY}
	${Boost_PROGRAM_OPTIONS_LIBRARY})

include_directories(${CMAKE_CURRENT_BINARY_DIR})
protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS EXPORT_MACRO DLL_EXPORT aws/kinesis/protobuf/messages.proto aws/kinesis/protobuf/config.proto)
set_source_files_properties(${PROTO_SRCS} ${PROTO_HDRS} PROPERTIES GENERATED TRUE)

set(THIRD_PARTY_LIBS
  ${CMAKE_THREAD_LIBS_INIT}     
  ${DEFAULT_BOOST_LIBS} 
  ${Protobuf_LIBRARIES}
  aws-cpp-sdk-kinesis
  aws-cpp-sdk-monitoring)

include_directories(${Boost_INCLUDE_DIRS})
include_directories(${Protobuf_INCLUDE_DIR})

add_executable(kinesis_producer ${SOURCE_FILES} aws/kinesis/main.cc ${PROTO_SRCS})
target_link_libraries(kinesis_producer ${THIRD_PARTY_LIBS})

add_executable(test_driver ${SOURCE_FILES} aws/kinesis/test_driver.cc ${PROTO_SRCS})
target_link_libraries(test_driver ${THIRD_PARTY_LIBS})

add_executable(tests ${SOURCE_FILES} ${TESTS_SOURCE} aws/kinesis/test/test.cc ${PROTO_SRCS})
target_link_libraries(tests ${THIRD_PARTY_LIBS} ${Boost_UNIT_TEST_FRAMEWORK_LIBRARY})

