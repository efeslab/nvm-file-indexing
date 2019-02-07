cmake_minimum_required(VERSION 3.1)
project(HashTable)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(PROJECT_LINK_LIBS common.so)
include_directories("../common")

file(GLOB hashtable_src
    "*.h"
    "*.c"
)

message(${hashtable_src})

add_library(HashTable SHARED ${hashtable_src})
