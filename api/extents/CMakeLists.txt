cmake_minimum_required(VERSION 3.1)
project(HashTable)
include(../cmake/CMakeCommon.txt)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
#set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra")
include_directories("..")

file(GLOB extents_src
    "*.h"
    "*.c"
)

add_library(ExtentTree OBJECT ${extents_src})
set_property(TARGET ExtentTree PROPERTY POSITION_INDEPENDENT_CODE ON)
