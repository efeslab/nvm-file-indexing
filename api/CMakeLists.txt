cmake_minimum_required (VERSION 3.1)
project (NVMIndexingAPI)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -mavx512f -mavx512dq -mavx512vl -msse -mavx2")
set (VERSION_MAJOR 0)
set (VERSION_MINOR 1)

add_subdirectory(common)
add_subdirectory(hashtable)
add_subdirectory(cuckoohash)
add_subdirectory(extents)
add_subdirectory(levelhash)
add_subdirectory(radixtree)
#add_subdirectory(p-hot)
#link_directories(p-hot/boost_1_73_0/stage/lib)
add_subdirectory(test)

file(GLOB api_src
    "*.h"
    "*.c"
)

add_library(NVMIndexingAPI SHARED
            ${api_src}
            $<TARGET_OBJECTS:Common>
            $<TARGET_OBJECTS:HashTable>
            $<TARGET_OBJECTS:CuckooHash>
            $<TARGET_OBJECTS:ExtentTree>
            $<TARGET_OBJECTS:LevelHashing>
            $<TARGET_OBJECTS:RadixTree>
            #$<TARGET_OBJECTS:P-HOT>
            )
target_link_libraries(NVMIndexingAPI m pmem json-c pthread boost_thread boost_system pmemobj)
