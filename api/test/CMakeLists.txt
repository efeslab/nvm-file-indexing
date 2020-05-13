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

file(GLOB test_src
    "*.hpp"
    "*.cpp"
)

add_executable(TestSuite
               ${test_src}
               $<TARGET_OBJECTS:Common>
               $<TARGET_OBJECTS:HashTable>
               $<TARGET_OBJECTS:CuckooHash>
               $<TARGET_OBJECTS:ExtentTree>
               $<TARGET_OBJECTS:LevelHashing>
               $<TARGET_OBJECTS:RadixTree>
               )
target_link_libraries(TestSuite gtest pmem json-c)
