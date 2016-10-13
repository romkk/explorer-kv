cmake_minimum_required (VERSION 2.6)

project (explorer)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/Modules/")

#
# cmake -DCMAKE_BUILD_TYPE=Debug ..
# cmake -DCMAKE_BUILD_TYPE=Release ..
#
message("CMAKE_BUILD_TYPE = ${CMAKE_BUILD_TYPE}")
IF(CMAKE_BUILD_TYPE MATCHES Debug)
  message("Debug build.")
  set(CMAKE_CXX_FLAGS "-g -O0 -Wall")
ELSEIF(CMAKE_BUILD_TYPE MATCHES Release)
  message("Release build.")
  set(CMAKE_CXX_FLAGS "-O2 -Wall")
ELSE()
  message("Some other build type.")
ENDIF()

set(CMAKE_CXX_COMPILER "g++")
set(CMAKE_C_COMPILER "gcc")
set(CMAKE_CXX_COMPILER_ARG1 "-std=c++0x ")

set(secp256k1_DIR "${CMAKE_CURRENT_BINARY_DIR}/../src/bitcoin/secp256k1")
set(secp256k1_LIBRARIES "${secp256k1_DIR}/.libs/libsecp256k1.a")
if (NOT EXISTS ${secp256k1_LIBRARIES})
  message(STATUS "build secp256k1...")
  execute_process(WORKING_DIRECTORY "${secp256k1_DIR}" COMMAND ./autogen.sh)
  execute_process(WORKING_DIRECTORY "${secp256k1_DIR}" COMMAND ./configure)
  execute_process(WORKING_DIRECTORY "${secp256k1_DIR}" COMMAND make)
endif()

set(ROCKSDB_DIR "${CMAKE_CURRENT_BINARY_DIR}/rocksdb-4.1")
set(ROCKSDB_LIBRARIES "${ROCKSDB_DIR}/librocksdb.a")
if (NOT EXISTS ${ROCKSDB_LIBRARIES})
  message(STATUS "get rocksdb...")
  execute_process(COMMAND wget https://github.com/facebook/rocksdb/archive/v4.1.tar.gz -O rocksdb-4.1.tar.gz)
  execute_process(COMMAND tar zxf rocksdb-4.1.tar.gz)
  execute_process(COMMAND rm rocksdb-4.1.tar.gz)
  execute_process(WORKING_DIRECTORY "${ROCKSDB_DIR}"
                  COMMAND make -j4 static_lib)
endif()

find_package(OpenSSL)
if(NOT OPENSSL_FOUND)
  message(FATAL_ERROR "OpenSSL not found!")
endif(NOT OPENSSL_FOUND)

find_package(Boost 1.36.0 COMPONENTS thread filesystem system regex program_options)
if(NOT Boost_FOUND)
  message(FATAL_ERROR "Boost not found!")
endif(NOT Boost_FOUND)

find_package(Mysql)
if(NOT MYSQL_FOUND)
  message(FATAL_ERROR "Mysql not found!")
endif(NOT MYSQL_FOUND)

find_package(TcMalloc)
if(NOT TCMALLOC_FOUND)
  message(FATAL_ERROR "TcMalloc not found!")
endif(NOT TCMALLOC_FOUND)

set(LIBEVHTP_INCLUDE_DIRS "/usr/local/include/evhtp")
include_directories(src test src/bitcoin src/bitcoin/secp256k1/include ${OPENSSL_INCLUDE_DIR} ${Boost_INCLUDE_DIRS} ${MYSQL_INCLUDE_DIR} ${LIBEVHTP_INCLUDE_DIRS})
set(THRID_LIBRARIES ${MYSQL_LIBRARIES} ${OPENSSL_CRYPTO_LIBRARY} ${OPENSSL_SSL_LIBRARY} ${Boost_LIBRARIES} ${ROCKSDB_LIBRARIES} ${TCMALLOC_LIBRARY} ${secp256k1_LIBRARIES} pthread curl z snappy bz2 event evhtp)

file(GLOB LIB_SOURCES src/*.cc src/bitcoin/*.cpp src/bitcoin/consensus/*.cpp src/bitcoin/crypto/*.cpp src/bitcoin/primitives/*.cpp src/bitcoin/script/*.cpp src/bitcoin/support/*.cpp)
add_library(explorer STATIC ${LIB_SOURCES})

file(GLOB TPARSER_SOURCES src/tparser/*.cc)
add_executable(tparser ${TPARSER_SOURCES})
target_link_libraries(tparser explorer ${THRID_LIBRARIES})

file(GLOB TPARSER_SOURCES src/tnotify/*.cc)
add_executable(tnotify ${TPARSER_SOURCES})
target_link_libraries(tnotify explorer ${THRID_LIBRARIES})

file(GLOB LOG1PRODUCER_SOURCES src/log1producer/*.cc)
add_executable(log1producer ${LOG1PRODUCER_SOURCES})
target_link_libraries(log1producer explorer ${THRID_LIBRARIES})

file(GLOB LOG2PRODUCER_SOURCES src/log2producer/*.cc)
add_executable(log2producer ${LOG2PRODUCER_SOURCES})
target_link_libraries(log2producer explorer ${THRID_LIBRARIES})

file(GLOB_RECURSE TEST_SOURCES test/*.cc)
add_executable(unittest ${TEST_SOURCES})
target_link_libraries(unittest explorer ${THRID_LIBRARIES})

file(GLOB HTTPD_SOURCES src/apiserver/*.cc)
add_executable(apiserver ${HTTPD_SOURCES})
target_link_libraries(apiserver explorer ${THRID_LIBRARIES})

