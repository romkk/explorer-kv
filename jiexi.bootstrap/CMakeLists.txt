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

find_package(TcMalloc)
if(NOT TCMALLOC_FOUND)
  message(FATAL_ERROR "TcMalloc not found!")
endif(NOT TCMALLOC_FOUND)

include_directories(src test src/bitcoin src/bitcoin/secp256k1/include ${OPENSSL_INCLUDE_DIR} ${Boost_INCLUDE_DIRS})
set(THRID_LIBRARIES ${OPENSSL_CRYPTO_LIBRARY} ${OPENSSL_SSL_LIBRARY} ${Boost_LIBRARIES} ${ROCKSDB_LIBRARIES} ${TCMALLOC_LIBRARY} ${secp256k1_LIBRARIES} pthread curl z snappy bz2 event)

file(GLOB LIB_SOURCES src/*.cc src/bitcoin/*.cpp src/bitcoin/consensus/*.cpp src/bitcoin/crypto/*.cpp src/bitcoin/primitives/*.cpp src/bitcoin/script/*.cpp src/bitcoin/support/*.cpp)
add_library(explorer STATIC ${LIB_SOURCES})

file(GLOB PREADDR_SOURCES src/tparser/MainPreAddress.cc)
add_executable(pre_addr ${PREADDR_SOURCES})
target_link_libraries(pre_addr explorer ${THRID_LIBRARIES})

file(GLOB PRETX_SOURCES src/tparser/MainPreTx.cc)
add_executable(pre_tx ${PRETX_SOURCES})
target_link_libraries(pre_tx explorer ${THRID_LIBRARIES})

file(GLOB PREPARSER_SOURCES src/tparser/MainPreParser.cc)
add_executable(pre_parser ${PREPARSER_SOURCES})
target_link_libraries(pre_parser explorer ${THRID_LIBRARIES})

file(GLOB COMPACT_SOURCES src/tparser/MainCompactDB.cc)
add_executable(compactdb ${COMPACT_SOURCES})
target_link_libraries(compactdb explorer ${THRID_LIBRARIES})

file(GLOB BLOCK_IMPORTER_SOURCES src/tparser/MainBlockImporter.cc)
add_executable(blkimport ${BLOCK_IMPORTER_SOURCES})
target_link_libraries(blkimport explorer ${THRID_LIBRARIES})

file(GLOB_RECURSE TEST_SOURCES test/*.cc)
add_executable(unittest ${TEST_SOURCES})
target_link_libraries(unittest explorer ${THRID_LIBRARIES})
