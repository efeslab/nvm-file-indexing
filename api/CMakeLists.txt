cmake_minimum_required (VERSION 3.1)
project (NVMIndexingAPI)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra")
set (VERSION_MAJOR 0)
set (VERSION_MINOR 1)

add_subdirectory(common)
add_subdirectory(hashtable)
add_subdirectory(extents)
add_subdirectory(test)

file(GLOB api_src
    "*.h"
    "*.c"
)

add_library(NVMIndexingAPI SHARED
            ${api_src}
            $<TARGET_OBJECTS:Common>
            $<TARGET_OBJECTS:HashTable>
            $<TARGET_OBJECTS:ExtentTree>
            )
