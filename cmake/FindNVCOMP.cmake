# Locate NVIDIA nvCOMP when CONFIG-mode discovery fails.
#
# Honors NVCOMP_ROOT (cache var or env var) and falls back to standard install
# locations. Defines an imported target nvcomp::nvcomp on success.

include(FindPackageHandleStandardArgs)

set(_nvcomp_search_roots
    ${NVCOMP_ROOT}
    $ENV{NVCOMP_ROOT}
    /usr/local/nvcomp
    /opt/nvidia/nvcomp
    "C:/Program Files/NVIDIA Corporation/nvcomp"
)

find_path(NVCOMP_INCLUDE_DIR
    NAMES nvcomp.h
    HINTS ${_nvcomp_search_roots}
    PATH_SUFFIXES include
)

find_library(NVCOMP_LIBRARY
    NAMES nvcomp
    HINTS ${_nvcomp_search_roots}
    PATH_SUFFIXES lib lib64
)

find_package_handle_standard_args(NVCOMP
    REQUIRED_VARS NVCOMP_INCLUDE_DIR NVCOMP_LIBRARY
)

if(NVCOMP_FOUND AND NOT TARGET nvcomp::nvcomp)
    add_library(nvcomp::nvcomp UNKNOWN IMPORTED)
    set_target_properties(nvcomp::nvcomp PROPERTIES
        IMPORTED_LOCATION             "${NVCOMP_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${NVCOMP_INCLUDE_DIR}"
    )
endif()

mark_as_advanced(NVCOMP_INCLUDE_DIR NVCOMP_LIBRARY)
