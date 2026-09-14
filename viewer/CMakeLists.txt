find_package(PkgConfig REQUIRED)

pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)

add_library(viewer STATIC
    sdl_display_manager.c
    sdl_display_sdl.c
)

target_include_directories(viewer PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(viewer PUBLIC
    core
)

target_link_libraries(viewer PRIVATE
    PkgConfig::SDL2
    ipc_compile_options
)
