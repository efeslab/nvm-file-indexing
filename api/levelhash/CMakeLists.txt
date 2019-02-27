cmake_minimum_required(VERSION 3.1)
project(LevelHashing)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra")
include_directories("..")

file(GLOB levelhashing_src
    "*.h"
    "*.c"
)

add_library(LevelHashing OBJECT ${levelhashing_src})
set_property(TARGET LevelHashing PROPERTY POSITION_INDEPENDENT_CODE ON)
