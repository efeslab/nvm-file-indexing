cmake_minimum_required(VERSION 3.1)
project(RadixTree)
include(../cmake/CMakeLists.txt)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -mrtm -mstackrealign")
include_directories("..")

file(GLOB radixtree_src
    "*.h"
    "*.c"
)

add_library(RadixTree OBJECT ${radixtree_src})
set_property(TARGET RadixTree PROPERTY POSITION_INDEPENDENT_CODE ON)
