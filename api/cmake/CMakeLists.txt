# vim: set filetype=cmake:
execute_process(COMMAND lscpu COMMAND grep "avx512" COMMAND tr -d [:blank:] OUTPUT_VARIABLE ARCH_FEATURES)
include(CheckCCompilerFlag)
include(CheckCXXCompilerFlag)
if(ARCH_FEATURES STREQUAL "") 
    foreach(FLAG IN ITEMS "-mno-avx512f")
        check_c_compiler_flag(${FLAG} HAS_C_FLAG)
        if(${HAS_C_FLAG})
            #message("Adding ${FLAG} to CMAKE_C_FLAGS...")
            set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${FLAG}")
        endif()

        check_cxx_compiler_flag(${FLAG} HAS_CXX_FLAG)
        if(${HAS_CXX_FLAG})
            #message("Adding ${FLAG} to CMAKE_CXX_FLAGS...")
            set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${FLAG}")
        endif()
    endforeach()
    #else()
    #message("'${ARCH_FEATURES}'")
else()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mavx512f")
endif()
