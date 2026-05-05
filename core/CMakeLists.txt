# ---------- Generated config header -------------------------------------------

# config.hpp.in checks COLLAPSAR_HAS_CUDA (a 0/1 flag set by the top-level
# CMakeLists once it has actually located CUDA + nvCOMP). Keep it in sync
# with the option so existing call sites (#if COLLAPSAR_HAS_CUDA) still work.
if(NOT DEFINED COLLAPSAR_HAS_CUDA)
    set(COLLAPSAR_HAS_CUDA ${COLLAPSAR_WITH_CUDA})
endif()

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/include/collapsar/config.hpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/collapsar/config.hpp"
    @ONLY
)

# ---------- Source layout -----------------------------------------------------

set(COLLAPSAR_CORE_SOURCES
    src/util/log.cpp
    src/util/thread_pool.cpp
    src/io/pinned_buffer.cpp
    src/io/file_scanner.cpp
    src/io/async_reader.cpp
    src/codec/crc32.cpp
    src/codec/zlib_deflate_codec.cpp
    src/codec/codec_factory.cpp
    src/pipeline/pipeline.cpp
    src/format/zip_writer.cpp
    src/format/gzip_writer.cpp
    src/format/writer_factory.cpp
    src/job_handle.cpp
    src/capabilities.cpp
    src/engine.cpp
)

if(COLLAPSAR_WITH_CUDA)
    list(APPEND COLLAPSAR_CORE_SOURCES
        src/codec/nvcomp_deflate_codec.cu
    )
endif()

# ---------- Library target ----------------------------------------------------

add_library(collapsar_core STATIC ${COLLAPSAR_CORE_SOURCES})
add_library(collapsar::core ALIAS collapsar_core)

target_include_directories(collapsar_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/generated>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_features(collapsar_core PUBLIC cxx_std_20)

target_link_libraries(collapsar_core
    PUBLIC  Threads::Threads
    PRIVATE ZLIB::ZLIB
)

if(COLLAPSAR_WITH_CUDA)
    target_link_libraries(collapsar_core
        PRIVATE
            CUDA::cudart
            $<TARGET_NAME_IF_EXISTS:nvcomp::nvcomp>
    )
    if(NOT TARGET nvcomp::nvcomp)
        # Fallback: linker flags from FindNVCOMP.
        target_link_libraries(collapsar_core PRIVATE ${NVCOMP_LIBRARY})
        target_include_directories(collapsar_core PRIVATE ${NVCOMP_INCLUDE_DIR})
    endif()

    set_target_properties(collapsar_core PROPERTIES
        CUDA_SEPARABLE_COMPILATION ON
        POSITION_INDEPENDENT_CODE  ON
    )
endif()

set_target_properties(collapsar_core PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    CXX_VISIBILITY_PRESET    hidden
)
