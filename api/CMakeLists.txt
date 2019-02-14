cmake_minimum_required (VERSION 3.1)
project (NVMIndexingAPI)
set (VERSION_MAJOR 0)
set (VERSION_MINOR 1)

add_subdirectory(common)
add_subdirectory(hashtable)
add_subdirectory(test)

file(GLOB api_src
    "*.h"
    "*.c"
)

add_library(NVMIndexingAPI SHARED
            ${api_src}
            $<TARGET_OBJECTS:Common>
            $<TARGET_OBJECTS:HashTable>)
