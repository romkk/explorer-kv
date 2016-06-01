cmake_minimum_required (VERSION 2.6)

project (explorer)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/Modules/")
set(CMAKE_BUILD_TYPE Release)

set(CMAKE_CXX_COMPILER "g++")
set(CMAKE_C_COMPILER "gcc")
set(CMAKE_CXX_COMPILER_ARG1 "-std=c++0x ")
set(CMAKE_CXX_FLAGS "-g -O2")

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

find_package(Boost 1.36.0 COMPONENTS thread filesystem system regex)
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

set(THRID_LIBRARIES "-lz -lpthread -lsnappy -lbz2")

include_directories(src src/bitcoin test ${OPENSSL_INCLUDE_DIR} ${Boost_INCLUDE_DIRS} ${MYSQL_INCLUDE_DIR})

file(GLOB LIB_SOURCES src/*.cc src/bitcoin/*.cpp)
add_library(explorer STATIC ${LIB_SOURCES})

file(GLOB PREADDR_SOURCES src/tparser/MainPreAddress.cc)
add_executable(pre_addr ${PREADDR_SOURCES})
target_link_libraries(pre_addr explorer ${MYSQL_LIBRARIES} ${OPENSSL_CRYPTO_LIBRARY} ${OPENSSL_SSL_LIBRARY} ${Boost_LIBRARIES} ${ROCKSDB_LIBRARIES} ${TCMALLOC_LIBRARY} ${THRID_LIBRARIES})

file(GLOB PRETX_SOURCES src/tparser/MainPreTx.cc)
add_executable(pre_tx ${PRETX_SOURCES})
target_link_libraries(pre_tx explorer ${MYSQL_LIBRARIES} ${OPENSSL_CRYPTO_LIBRARY} ${OPENSSL_SSL_LIBRARY} ${Boost_LIBRARIES} ${ROCKSDB_LIBRARIES} ${TCMALLOC_LIBRARY} ${THRID_LIBRARIES})

file(GLOB PRETXDB_SOURCES src/tparser/MainPreTxDB.cc)
add_executable(pre_txdb ${PRETXDB_SOURCES})
target_link_libraries(pre_txdb explorer ${MYSQL_LIBRARIES} ${OPENSSL_CRYPTO_LIBRARY} ${OPENSSL_SSL_LIBRARY} ${Boost_LIBRARIES} ${ROCKSDB_LIBRARIES} ${TCMALLOC_LIBRARY} ${THRID_LIBRARIES})

file(GLOB PREPARSER_SOURCES src/tparser/MainPreParser.cc)
add_executable(pre_parser ${PREPARSER_SOURCES})
target_link_libraries(pre_parser explorer ${MYSQL_LIBRARIES} ${OPENSSL_CRYPTO_LIBRARY} ${OPENSSL_SSL_LIBRARY} ${Boost_LIBRARIES} ${ROCKSDB_LIBRARIES} ${TCMALLOC_LIBRARY} ${THRID_LIBRARIES})

file(GLOB COMPACT_SOURCES src/tparser/MainCompactDB.cc)
add_executable(compactdb ${COMPACT_SOURCES})
target_link_libraries(compactdb explorer ${MYSQL_LIBRARIES} ${OPENSSL_CRYPTO_LIBRARY} ${OPENSSL_SSL_LIBRARY} ${Boost_LIBRARIES} ${ROCKSDB_LIBRARIES} ${TCMALLOC_LIBRARY} ${THRID_LIBRARIES})

file(GLOB BLOCK_IMPORTER_SOURCES src/tparser/MainBlockImporter.cc)
add_executable(blkimport ${BLOCK_IMPORTER_SOURCES})
target_link_libraries(blkimport explorer ${MYSQL_LIBRARIES} ${OPENSSL_CRYPTO_LIBRARY} ${OPENSSL_SSL_LIBRARY} ${Boost_LIBRARIES} ${ROCKSDB_LIBRARIES} ${TCMALLOC_LIBRARY} ${THRID_LIBRARIES})

file(GLOB_RECURSE TEST_SOURCES test/*.cc)
add_executable(unittest ${TEST_SOURCES})
target_link_libraries(unittest explorer ${MYSQL_LIBRARIES} ${OPENSSL_CRYPTO_LIBRARY} ${OPENSSL_SSL_LIBRARY} ${Boost_LIBRARIES} ${ROCKSDB_LIBRARIES} ${TCMALLOC_LIBRARY} ${THRID_LIBRARIES})
