add_executable(collapsar_cli main.cpp)
set_target_properties(collapsar_cli PROPERTIES OUTPUT_NAME collapsar)

target_link_libraries(collapsar_cli PRIVATE collapsar::core)

if(MSVC)
    target_compile_options(collapsar_cli PRIVATE /W4 /permissive-)
else()
    target_compile_options(collapsar_cli PRIVATE -Wall -Wextra -Wpedantic)
endif()
