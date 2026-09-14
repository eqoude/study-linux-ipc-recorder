add_library(capture STATIC
    capture_manager.c
    device_scanner.c
    fake_capture.c
    v4l2_capture.c
)

target_include_directories(capture PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(capture PUBLIC
    core
)

target_link_libraries(capture PRIVATE
    ipc_compile_options
)
