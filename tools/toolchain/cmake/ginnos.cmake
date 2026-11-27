# a CMake toolchain file for the ginnOS toolchain (tools/toolchain).
#
# usage: cmake -DCMAKE_TOOLCHAIN_FILE=tools/toolchain/cmake/ginnos.cmake
#              -DGINNOS_CROSS=/path/to/x86_64-ginnos- ...
# (or GINNOS_CROSS in the environment). the compiler knows its sysroot;
# libraries and headers are looked for only there.

if(NOT GINNOS_CROSS)
  set(GINNOS_CROSS "$ENV{GINNOS_CROSS}")
endif()
if(NOT GINNOS_CROSS)
  message(FATAL_ERROR "set GINNOS_CROSS to the toolchain prefix, as /path/to/x86_64-ginnos-")
endif()
# try_compile loads this file again in a new project: give it the prefix
set(ENV{GINNOS_CROSS} "${GINNOS_CROSS}")
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES GINNOS_CROSS)

get_filename_component(_ginnos_name "${GINNOS_CROSS}" NAME)
string(REGEX REPLACE "-ginnos-$" "" _ginnos_arch "${_ginnos_name}")

set(CMAKE_SYSTEM_NAME ginnOS)
set(CMAKE_SYSTEM_PROCESSOR "${_ginnos_arch}")
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}")

set(CMAKE_C_COMPILER "${GINNOS_CROSS}gcc")
set(CMAKE_CXX_COMPILER "${GINNOS_CROSS}g++")
set(CMAKE_AR "${GINNOS_CROSS}ar" CACHE FILEPATH "")
set(CMAKE_RANLIB "${GINNOS_CROSS}ranlib" CACHE FILEPATH "")
set(CMAKE_STRIP "${GINNOS_CROSS}strip" CACHE FILEPATH "")

execute_process(COMMAND "${GINNOS_CROSS}gcc" -print-sysroot
                OUTPUT_VARIABLE _ginnos_sysroot OUTPUT_STRIP_TRAILING_WHITESPACE)
set(CMAKE_FIND_ROOT_PATH "${_ginnos_sysroot}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

set(CMAKE_EXE_LINKER_FLAGS_INIT "-static")
