cmake_minimum_required (VERSION 3.1)
project (NVMIndexingAPI)
set (VERSION_MAJOR 0)
set (VERSION_MINOR 1)

add_subdirectory(common)
add_subdirectory(hashtable)

add_library(NVMIndexingAPI SHARED foo.c)
target_link_libraries(NVMIndexingAPI HashTable Common)
