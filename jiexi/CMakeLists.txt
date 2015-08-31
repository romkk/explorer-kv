cmake_minimum_required (VERSION 2.6)

project (explorer)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/Modules/")
set(CMAKE_BUILD_TYPE Release)

set(CMAKE_CXX_COMPILER "g++")
set(CMAKE_C_COMPILER "gcc")
set(CMAKE_CXX_COMPILER_ARG1 "-std=c++0x ")
set(CMAKE_CXX_FLAGS "-g -O0")

set(SSDB_DIR "${CMAKE_CURRENT_BINARY_DIR}/ssdb-1.9.0")
SET(SSDB_LIBRARIES "${SSDB_DIR}/api/cpp/libssdb-client.a")
if (NOT EXISTS ${SSDB_LIBRARIES})
  message(STATUS "get ssdb...")
  execute_process(COMMAND wget https://github.com/ideawu/ssdb/archive/1.9.0.tar.gz)
  execute_process(COMMAND tar zxf 1.9.0.tar.gz)
  execute_process(COMMAND rm 1.9.0.tar.gz)
  execute_process(WORKING_DIRECTORY "${SSDB_DIR}"
                  COMMAND make)
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

include_directories(src test ${OPENSSL_INCLUDE_DIR} ${Boost_INCLUDE_DIRS} ${MYSQL_INCLUDE_DIR})

file(GLOB LIB_SOURCES src/*.cc src/bitcoin/*.cpp)
add_library(explorer STATIC ${LIB_SOURCES})

file(GLOB TPARSER_SOURCES src/tparser/*.cc)
add_executable(tparser ${TPARSER_SOURCES})
target_link_libraries(tparser explorer ${MYSQL_LIBRARIES} ${OPENSSL_CRYPTO_LIBRARY} ${OPENSSL_SSL_LIBRARY} ${Boost_LIBRARIES} ${SSDB_LIBRARIES} -lcurl)

file(GLOB_RECURSE TEST_SOURCES test/*.cc)
add_executable(unittest ${TEST_SOURCES})
target_link_libraries(unittest explorer ${MYSQL_LIBRARIES} ${OPENSSL_CRYPTO_LIBRARY} ${OPENSSL_SSL_LIBRARY} ${Boost_LIBRARIES} -lpthread -lcurl)
