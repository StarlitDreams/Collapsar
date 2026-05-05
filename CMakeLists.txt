cmake_minimum_required(VERSION 3.26)

project(Collapsar
    VERSION 0.1.0
    DESCRIPTION "GPU-accelerated file compression"
    LANGUAGES CXX
)

# ---------- Options ------------------------------------------------------------

option(COLLAPSAR_WITH_CUDA   "Build the GPU backend (requires CUDA + nvCOMP)" OFF)
option(COLLAPSAR_BUILD_CLI   "Build the command-line driver"                  ON)
option(COLLAPSAR_BUILD_TESTS "Build unit tests"                               ON)
option(COLLAPSAR_BUILD_BRIDGE "Build the Windows C-ABI bridge for the GUI"    ${WIN32})

# ---------- Global settings ----------------------------------------------------

set(CMAKE_CXX_STANDARD          20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS         OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)
endif()

# Put all final binaries in build/<config>/bin so the CLI exe is easy to find,
# regardless of generator (Ninja vs Visual Studio multi-config).
if(NOT DEFINED CMAKE_RUNTIME_OUTPUT_DIRECTORY)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
endif()
if(NOT DEFINED CMAKE_LIBRARY_OUTPUT_DIRECTORY)
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
endif()

if(MSVC)
    add_compile_options(/W4 /permissive- /Zc:__cplusplus /utf-8)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

# Help find vcpkg-installed zlib on Windows when no toolchain file was passed.
if(WIN32 AND NOT CMAKE_TOOLCHAIN_FILE)
    foreach(_vcpkg_root "$ENV{VCPKG_ROOT}" "C:/vcpkg" "C:/dev/vcpkg")
        if(_vcpkg_root AND EXISTS "${_vcpkg_root}/installed/x64-windows/include/zlib.h")
            list(APPEND CMAKE_PREFIX_PATH "${_vcpkg_root}/installed/x64-windows")
            message(STATUS "Using vcpkg zlib from ${_vcpkg_root}/installed/x64-windows")
            break()
        endif()
    endforeach()
endif()

# ---------- Third-party --------------------------------------------------------

find_package(Threads REQUIRED)
find_package(ZLIB    REQUIRED)

# Resolve CUDA + nvCOMP. If the user asked for CUDA but either dependency is
# missing, downgrade to a CPU-only build with a warning rather than failing
# the configure step — this keeps the CLI buildable on machines that don't
# have nvCOMP installed (e.g. CI runners, fresh dev boxes).
set(COLLAPSAR_HAS_CUDA 0)
if(COLLAPSAR_WITH_CUDA)
    include(CheckLanguage)
    check_language(CUDA)
    if(NOT CMAKE_CUDA_COMPILER)
        message(WARNING "COLLAPSAR_WITH_CUDA=ON but no CUDA compiler was found; building without GPU backend.")
        set(COLLAPSAR_WITH_CUDA OFF CACHE BOOL "Build the GPU backend" FORCE)
    else()
        enable_language(CUDA)
        set(CMAKE_CUDA_STANDARD          17)
        set(CMAKE_CUDA_STANDARD_REQUIRED ON)
        if(NOT DEFINED CMAKE_CUDA_ARCHITECTURES)
            # Pascal+ covers GTX 10xx through Hopper; bump as needed.
            set(CMAKE_CUDA_ARCHITECTURES 70 75 80 86 89 90)
        endif()
        find_package(CUDAToolkit REQUIRED)

        find_package(NVCOMP CONFIG QUIET)
        if(NOT NVCOMP_FOUND)
            include(FindNVCOMP)
        endif()

        if(NVCOMP_FOUND OR TARGET nvcomp::nvcomp)
            set(COLLAPSAR_HAS_CUDA 1)
        else()
            message(WARNING "COLLAPSAR_WITH_CUDA=ON but nvCOMP was not found (set NVCOMP_ROOT). Building without GPU backend.")
            set(COLLAPSAR_WITH_CUDA OFF CACHE BOOL "Build the GPU backend" FORCE)
        endif()
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
message(STATUS "  Build GUI bridge  : ${COLLAPSAR_BUILD_BRIDGE}")
message(STATUS "  Compiler          : ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "  Runtime output    : ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
message(STATUS "")
