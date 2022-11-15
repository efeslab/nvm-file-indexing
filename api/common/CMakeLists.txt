cmake_minimum_required(VERSION 3.1)
project(Common)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -pedantic -Wextra")

file(GLOB common_src
    "*.h"
    "*.c"
)

add_library(Common OBJECT ${common_src})
set_property(TARGET Common PROPERTY POSITION_INDEPENDENT_CODE ON)