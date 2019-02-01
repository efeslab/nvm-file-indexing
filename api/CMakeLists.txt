cmake_minimum_required (VERSION 2.8)
project (NVMIndexingAPI)
set (VERSION_MAJOR 0)
set (VERSION_MINOR 1)

add_subdirectory(hashtable)
add_library(NVMIndexingAPI SHARED foo.c)
target_link_libraries(NVMIndexingAPI hashtable)
