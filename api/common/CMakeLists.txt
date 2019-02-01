cmake_minimum_required(VERSION 3.1)
project(Common)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

file(GLOB common_src
    "*.h"
    "*.c"
)

add_library(Common SHARED ${common_src})
