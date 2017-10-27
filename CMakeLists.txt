cmake_minimum_required(VERSION 3.5)
project(amazon_kinesis_producer)

set(THIRD_PARTY_LIB_DIR "${amazon_kinesis_producer_SOURCE_DIR}/third_party/lib")

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SOURCE_FILES
        aws/auth/mutable_static_creds_provider.h
        aws/kinesis/core/aggregator.h
        aws/kinesis/core/attempt.h
        aws/kinesis/core/collector.h
        aws/kinesis/core/configuration.h
        aws/kinesis/core/ipc_manager.cc
        aws/kinesis/core/ipc_manager.h
        aws/kinesis/core/kinesis_producer.cc
        aws/kinesis/core/kinesis_producer.h
        aws/kinesis/core/kinesis_record.cc
        aws/kinesis/core/kinesis_record.h
        aws/kinesis/core/limiter.h
        aws/kinesis/core/pipeline.h
        aws/kinesis/core/put_records_context.h
        aws/kinesis/core/put_records_request.h
        aws/kinesis/core/reducer.h
        aws/kinesis/core/retrier.cc
        aws/kinesis/core/retrier.h
        aws/kinesis/core/serializable_container.h
        aws/kinesis/core/shard_map.cc
        aws/kinesis/core/shard_map.h
        aws/kinesis/core/user_record.cc
        aws/kinesis/core/user_record.h
        aws/kinesis/protobuf/config.pb.cc
        aws/kinesis/protobuf/config.pb.h
        aws/kinesis/protobuf/config.proto
        aws/kinesis/protobuf/messages.pb.cc
        aws/kinesis/protobuf/messages.pb.h
        aws/kinesis/protobuf/messages.proto
        aws/metrics/accumulator.h
        aws/metrics/metric.h
        aws/metrics/metrics_constants.cc
        aws/metrics/metrics_constants.h
        aws/metrics/metrics_finder.h
        aws/metrics/metrics_index.cc
        aws/metrics/metrics_index.h
        aws/metrics/metrics_manager.cc
        aws/metrics/metrics_manager.h
        aws/mutex.h
        aws/utils/concurrent_hash_map.h
        aws/utils/concurrent_linked_queue.h
        aws/utils/executor.h
        aws/utils/io_service_executor.h
        aws/utils/logging.cc
        aws/utils/logging.h
        aws/utils/spin_lock.h
        aws/utils/time_sensitive.h
        aws/utils/time_sensitive_queue.h
        aws/utils/token_bucket.h
        aws/utils/utils.cc
        aws/utils/utils.h
        aws/utils/processing_statistics_logger.cc
        aws/utils/processing_statistics_logger.h)

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


include_directories(${CMAKE_CURRENT_SOURCE_DIR})


find_package(aws-sdk-cpp)
find_package(Boost)
find_package(Protobuf)
find_package(Threads)

set(BOOST_LIBS
  Boost_regex_LIBRARY
  Boost_thread_LIBRARY 
  Boost_log_LIBRARY
  Boost_system_LIBRARY
  Boost_random_LIBRARY
  Boost_filesystem_LIBRARY
  Boost_chrono_LIBRARY)

set(THIRD_PARTY_LIBS
  ${CMAKE_THREAD_LIBS_INIT}     
  ${BOOST_LIBS} 
  Protobuf_LIBRARY)

# include_directories(${Boost_INCLUDE_DIRS})
# include_directories(${Protobuf_INCLUDE_DIR})

add_executable(kinesis_producer ${SOURCE_FILES} aws/kinesis/main.cc)  
target_link_libraries(kinesis_producer ${THIRD_PARTY_LIBS} aws-sdk-cpp-kinesis 
  aws-sdk-cpp-monitoring)

add_executable(test_driver ${SOURCE_FILES} aws/kinesis/test_driver.cc)  
target_link_libraries(test_driver ${THIRD_PARTY_LIBS})

add_executable(tests ${SOURCE_FILES} ${TESTS_SOURCE} aws/kinesis/test/test.cc)  
target_link_libraries(tests ${THIRD_PARTY_LIBS} boost_unit_test_framework)

