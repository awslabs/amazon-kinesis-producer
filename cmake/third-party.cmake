find_package(aws-sdk-cpp REQUIRED)

# set(Boost_USE_STATIC_LIBS ON)
find_package(Boost REQUIRED COMPONENTS regex thread log system random filesystem chrono program_options unit_test_framework)
find_package(Protobuf REQUIRED)

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