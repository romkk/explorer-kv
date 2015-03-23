cmake_minimum_required (VERSION 2.6)

project (explorer)

set(CMAKE_BUILD_TYPE Release)

set(CMAKE_CXX_COMPILER "g++")
set(CMAKE_C_COMPILER "gcc")
set(CMAKE_CXX_COMPILER_ARG1 "-std=c++0x ")

execute_process(COMMAND mysql_config --libs_r OUTPUT_VARIABLE MYSQL_LIB OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(COMMAND mysql_config --include OUTPUT_VARIABLE MYSQL_INCLUDE OUTPUT_STRIP_TRAILING_WHITESPACE)

set(CMAKE_CXX_FLAGS "-g ${MYSQL_INCLUDE}")

include_directories(src test)

file(GLOB LIB_SOURCES src/*.cc src/bitcoin/*.cpp)
add_library(explorer STATIC ${LIB_SOURCES})

#file(GLOB SSERVER_SOURCES src/sserver/*.cc)
#add_executable(tparser ${SSERVER_SOURCES})
#target_link_libraries(tparser explorer ${MYSQL_LIB} -lssl -lcrypto)
#
#file(GLOB_RECURSE TEST_SOURCES test/*.cc)
#add_executable(unittest ${TEST_SOURCES})
#target_link_libraries(unittest explorer ${MYSQL_LIB} -lssl -lcrypto)
