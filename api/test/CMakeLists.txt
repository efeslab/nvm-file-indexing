cmake_minimum_required(VERSION 3.1)
project(TestSuite)
include(../cmake/CMakeLists.txt)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -mrtm -mstackrealign")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mrtm -mstackrealign")

add_subdirectory("googletest/googletest")
include_directories("googletest/googletest/include")

include_directories("..")
link_directories("../p-hot-boost_1_73_0/stage/lib/")

file(GLOB test_src
    "*.hpp"
    "*.cpp"
)

find_library(JemallocLib jemalloc)
find_library(TbbLib tbb)
find_package(Threads)

add_executable(TestSuite
               ${test_src}
               $<TARGET_OBJECTS:Common>
               $<TARGET_OBJECTS:HashTable>
               $<TARGET_OBJECTS:CuckooHash>
               $<TARGET_OBJECTS:ExtentTree>
               $<TARGET_OBJECTS:LevelHashing>
               $<TARGET_OBJECTS:RadixTree>
               #$<TARGET_OBJECTS:P-HOT>
               )
target_link_libraries(TestSuite gtest pmem json-c boost_system boost_thread atomic ${TbbLib} ${JemallocLib} ${CMAKE_THREAD_LIBS_INIT} pmemobj)
