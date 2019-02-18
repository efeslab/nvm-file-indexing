cmake_minimum_required(VERSION 3.1)
project(HashTable)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
include_directories("..")

file(GLOB extents_src
    "*.h"
    "*.c"
)

add_library(ExtentTree OBJECT ${extents_src})
set_property(TARGET ExtentTree PROPERTY POSITION_INDEPENDENT_CODE ON)
