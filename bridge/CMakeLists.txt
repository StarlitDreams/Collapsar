# Collapsar.Bridge — C++/CLI assembly that wraps collapsar_core for managed
# consumers (the WinUI 3 GUI). Only meaningful on Windows + MSVC.

if(NOT WIN32 OR NOT MSVC)
    message(FATAL_ERROR "Collapsar.Bridge can only be built with MSVC on Windows")
endif()

add_library(Collapsar.Bridge SHARED
    src/Bridge.cpp
    src/Bridge.h
    src/AssemblyInfo.cpp
)

# Enable C++/CLI mode on this target. The /clr switch is incompatible with
# /EHsc and /RTC*, so override the inherited project flags here.
set_target_properties(Collapsar.Bridge PROPERTIES
    COMMON_LANGUAGE_RUNTIME    ""        # /clr (Pure CLR — managed + native interop)
    DOTNET_TARGET_FRAMEWORK    "net8.0-windows"
    VS_DOTNET_REFERENCES       "System;System.Core"
    VS_GLOBAL_ROOTNAMESPACE    "Collapsar.Bridge"
)

target_compile_options(Collapsar.Bridge PRIVATE
    /clr
    /EHa                        # /clr requires /EHa rather than /EHsc
)

# /clr disables some optimisations and conflicts with these flags inherited
# from the top-level CMakeLists.
string(REGEX REPLACE "/EHsc" ""    CMAKE_CXX_FLAGS    "${CMAKE_CXX_FLAGS}")
string(REGEX REPLACE "/RTC[1csu]+" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")

target_link_libraries(Collapsar.Bridge PRIVATE collapsar::core)
target_compile_features(Collapsar.Bridge PRIVATE cxx_std_20)
