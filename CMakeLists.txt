cmake_minimum_required(VERSION 3.26)

project(Collapsar
    VERSION 0.1.0
    DESCRIPTION "GPU-accelerated file compression"
    LANGUAGES CXX
)

# ---------- Options ------------------------------------------------------------

option(COLLAPSAR_WITH_CUDA   "Build the GPU backend (requires CUDA + nvCOMP)" ON)
option(COLLAPSAR_BUILD_CLI   "Build the command-line driver"                  ON)
option(COLLAPSAR_BUILD_TESTS "Build unit tests"                               ON)
option(COLLAPSAR_BUILD_BRIDGE "Build the Windows C++/CLI bridge"              ${WIN32})

# ---------- Global settings ----------------------------------------------------

set(CMAKE_CXX_STANDARD          20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS         OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)
endif()

if(MSVC)
    add_compile_options(/W4 /permissive- /Zc:__cplusplus /utf-8)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

# ---------- Third-party --------------------------------------------------------

find_package(Threads REQUIRED)
find_package(ZLIB    REQUIRED)

if(COLLAPSAR_WITH_CUDA)
    enable_language(CUDA)
    set(CMAKE_CUDA_STANDARD          17)
    set(CMAKE_CUDA_STANDARD_REQUIRED ON)
    if(NOT DEFINED CMAKE_CUDA_ARCHITECTURES)
        # Pascal+ covers GTX 10xx through Hopper; bump as needed.
        set(CMAKE_CUDA_ARCHITECTURES 70 75 80 86 89 90)
    endif()
    find_package(CUDAToolkit REQUIRED)
    find_package(NVCOMP CONFIG)
    if(NOT NVCOMP_FOUND)
        include(FindNVCOMP)
    endif()
endif()

# ---------- Subdirectories -----------------------------------------------------

add_subdirectory(core)

if(COLLAPSAR_BUILD_CLI)
    add_subdirectory(cli)
endif()

if(COLLAPSAR_BUILD_TESTS)
    enable_testing()
    add_subdirectory(core/tests)
endif()

if(COLLAPSAR_BUILD_BRIDGE AND WIN32)
    add_subdirectory(bridge)
endif()

# ---------- Summary ------------------------------------------------------------

message(STATUS "")
message(STATUS "Collapsar configuration")
message(STATUS "  Version           : ${PROJECT_VERSION}")
message(STATUS "  CUDA backend      : ${COLLAPSAR_WITH_CUDA}")
message(STATUS "  Build CLI         : ${COLLAPSAR_BUILD_CLI}")
message(STATUS "  Build tests       : ${COLLAPSAR_BUILD_TESTS}")
message(STATUS "  Build C++/CLI     : ${COLLAPSAR_BUILD_BRIDGE}")
message(STATUS "  Compiler          : ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "")
