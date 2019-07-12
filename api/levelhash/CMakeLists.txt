cmake_minimum_required(VERSION 3.1)
project(LevelHashing)
include(../cmake/CMakeCommon.txt)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
#set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -march=native")
#set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -march=native -g -O0 -mno-avx512vpopcntdq -mno-avx512f -mno-avx5124fmaps -mno-avx5124vnniw")
include_directories("..")

file(GLOB levelhashing_src
    "*.h"
    "*.c"
)

add_library(LevelHashing OBJECT ${levelhashing_src})
set_property(TARGET LevelHashing PROPERTY POSITION_INDEPENDENT_CODE ON)
