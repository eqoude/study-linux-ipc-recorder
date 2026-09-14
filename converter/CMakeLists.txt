add_library(converter STATIC
    converter_manager.c
    fake_converter.c
    yuyv_to_yuv420_converter.c
)

target_include_directories(converter PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(converter PUBLIC
    core
)

target_link_libraries(converter PRIVATE
    ipc_compile_options
)
