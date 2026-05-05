add_executable(collapsar_cli main.cpp)
set_target_properties(collapsar_cli PROPERTIES OUTPUT_NAME collapsar)

target_link_libraries(collapsar_cli PRIVATE collapsar::core)

if(MSVC)
    target_compile_options(collapsar_cli PRIVATE /W4 /permissive-)
else()
    target_compile_options(collapsar_cli PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Copy runtime DLLs (vcpkg zlib, CUDA runtime, ...) next to the executable so
# `collapsar.exe` can be run from its build directory without setting PATH.
#
# We deliberately do NOT use $<TARGET_RUNTIME_DLLS> here: vcpkg ships zlib as
# z.lib + z.dll without the metadata that genex relies on, so it returns an
# empty list and the resulting `cmake -E copy_if_different <DEST>` invocation
# fails. Walking IMPORTED_LOCATION back to the install prefix is reliable.
if(WIN32)
    set(_zlib_imported "")
    if(TARGET ZLIB::ZLIB)
        get_target_property(_zlib_imported ZLIB::ZLIB IMPORTED_LOCATION_RELEASE)
        if(NOT _zlib_imported OR _zlib_imported STREQUAL "_zlib_imported-NOTFOUND")
            get_target_property(_zlib_imported ZLIB::ZLIB IMPORTED_LOCATION)
        endif()
    endif()
    if(NOT _zlib_imported OR _zlib_imported STREQUAL "_zlib_imported-NOTFOUND")
        set(_zlib_imported "${ZLIB_LIBRARIES}")
    endif()
    message(STATUS "collapsar_cli: zlib import lib = ${_zlib_imported}")
    if(_zlib_imported)
        # IMPORTED_LOCATION points at the import .lib (e.g. .../lib/z.lib).
        # The matching DLL lives in ../bin alongside it in vcpkg layouts.
        get_filename_component(_zlib_libdir "${_zlib_imported}" DIRECTORY)
        get_filename_component(_zlib_root   "${_zlib_libdir}"   DIRECTORY)
        file(GLOB _zlib_dlls
            "${_zlib_root}/bin/z*.dll"
            "${_zlib_root}/bin/zlib*.dll"
            "${_zlib_libdir}/z*.dll"
            "${_zlib_libdir}/zlib*.dll"
        )
        message(STATUS "collapsar_cli: zlib DLLs to copy = ${_zlib_dlls}")
        foreach(_dll IN LISTS _zlib_dlls)
            add_custom_command(TARGET collapsar_cli POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${_dll}" "$<TARGET_FILE_DIR:collapsar_cli>"
                VERBATIM
            )
        endforeach()
    endif()
endif()
