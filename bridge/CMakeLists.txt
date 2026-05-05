# Collapsar.Bridge — flat C ABI shared library wrapping collapsar_core.
#
# Managed callers (the WinUI 3 GUI) consume this DLL via P/Invoke. We avoid
# C++/CLI here on purpose: it requires a separate Visual Studio workload that
# isn't installed by default, conflicts with several of the project's normal
# warning/optimisation flags, and tends to break across MSVC point releases.
# A plain C ABI builds on every Windows + MSVC machine and is faster to
# iterate on.

if(NOT WIN32)
    message(FATAL_ERROR "Collapsar.Bridge currently targets Windows only")
endif()

add_library(collapsar_bridge SHARED
    src/c_bridge.cpp
    src/c_bridge.h
)

# Choose a stable output name regardless of the cmake target identifier.
set_target_properties(collapsar_bridge PROPERTIES
    OUTPUT_NAME "Collapsar.Bridge"
    PREFIX ""                         # don't add lib... on MinGW
)

target_link_libraries(collapsar_bridge PRIVATE collapsar::core)
target_compile_features(collapsar_bridge PRIVATE cxx_std_20)

if(MSVC)
    target_compile_options(collapsar_bridge PRIVATE /W4 /permissive-)
endif()

# Make sure the resulting DLL lands in the global runtime output directory
# (same place collapsar.exe is dropped) so the GUI csproj can pick it up via
# a single relative path that's identical across generators.
set_target_properties(collapsar_bridge PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
)
