cmake_minimum_required(VERSION 3.1)
project(HashTable)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(/usr/local/include)
include_directories(/usr/local/include/libpmemobj)
add_subdirectory(common)

file(GLOB hashtable_src
    "*.h"
    "*.c"
    "test_obj_hash.cpp"
    "objghash.cpp"
)
file(GLOB hashtable_src2
    "*.h"
    "*.c"
    "test_obj_hash2.cpp"
    "objghash.cpp"
)

set(EXE_NAME Test1)
set(EXE_NAME2 Test2)
add_executable(${EXE_NAME} ${hashtable_src})
target_link_libraries(${EXE_NAME} "-L/usr/local/lib -lpmemobj")
add_executable(${EXE_NAME2} ${hashtable_src2})
target_link_libraries(${EXE_NAME2} "-L/usr/local/lib -lpmemobj")