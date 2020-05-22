cmake_minimum_required(VERSION 3.1)
project(P-HOT)
include(../cmake/CMakeLists.txt)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -mrtm -mstackrealign -mbmi2")
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17 -march=native -mrtm -mcx16 -mavx -mavx2 -mbmi2 -mlzcnt -Wno-deprecated-declarations -Wall -Wextra -fno-builtin-malloc -fno-builtin-calloc -fno-builtin-realloc -fno-builtin-free -faligned-new=64 -DNDEBUG -mrtm")

include_directories("..")
include_directories("boost_1_73_0")

include_directories(P-HOT/libs/hot/commons/include 
                    P-HOT/libs/hot/rowex/include 
                    P-HOT/libs/idx/benchmark-helpers/include 
                    P-HOT/libs/idx/map-helpers/include 
                    P-HOT/libs/idx/content-helpers/include 
                    P-HOT/libs/idx/utils/include 
                    P-HOT/libs/profile-lib/include)

add_subdirectory("P-HOT")

file(GLOB p_hot_src
    "*.h"
    "*.cpp"
    "*.c"
)

add_library(P-HOT OBJECT ${p_hot_src})
set_property(TARGET P-HOT PROPERTY POSITION_INDEPENDENT_CODE ON)
