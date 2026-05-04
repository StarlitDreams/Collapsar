function(collapsar_add_test name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE collapsar::core ZLIB::ZLIB)
    target_include_directories(${name} PRIVATE
        ${CMAKE_SOURCE_DIR}/core/src
    )
    if(MSVC)
        target_compile_options(${name} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${name} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME ${name} COMMAND ${name})
endfunction()

collapsar_add_test(test_size_router)
collapsar_add_test(test_crc32)
collapsar_add_test(test_zip_writer)
collapsar_add_test(test_engine_pack)
