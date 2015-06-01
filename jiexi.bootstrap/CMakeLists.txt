cmake_minimum_required (VERSION 2.6)

project (explorer)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/Modules/")
set(CMAKE_BUILD_TYPE Release)

set(CMAKE_CXX_COMPILER "g++")
set(CMAKE_C_COMPILER "gcc")
set(CMAKE_CXX_COMPILER_ARG1 "-std=c++0x ")
set(CMAKE_CXX_FLAGS "-g -O0")

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

file(GLOB PREADDR_SOURCES src/tparser/MainPreAddress.cc)
add_executable(pre_addr ${PREADDR_SOURCES})
target_link_libraries(pre_addr explorer ${MYSQL_LIBRARIES} ${OPENSSL_CRYPTO_LIBRARY} ${OPENSSL_SSL_LIBRARY} ${Boost_LIBRARIES})

file(GLOB PRETX_SOURCES src/tparser/MainPreTx.cc)
add_executable(pre_tx ${PRETX_SOURCES})
target_link_libraries(pre_tx explorer ${MYSQL_LIBRARIES} ${OPENSSL_CRYPTO_LIBRARY} ${OPENSSL_SSL_LIBRARY} ${Boost_LIBRARIES})

file(GLOB PREPARSER_SOURCES src/tparser/MainPreParser.cc)
add_executable(pre_parser ${PREPARSER_SOURCES})
target_link_libraries(pre_parser explorer ${MYSQL_LIBRARIES} ${OPENSSL_CRYPTO_LIBRARY} ${OPENSSL_SSL_LIBRARY} ${Boost_LIBRARIES})

file(GLOB_RECURSE TEST_SOURCES test/*.cc)
add_executable(unittest ${TEST_SOURCES})
target_link_libraries(unittest explorer ${MYSQL_LIBRARIES} ${OPENSSL_CRYPTO_LIBRARY} ${OPENSSL_SSL_LIBRARY} ${Boost_LIBRARIES} -lpthread)
