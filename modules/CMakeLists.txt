add_library(modules STATIC
    module_register.c
)

target_include_directories(modules PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(modules PUBLIC
    core
    capture
    converter
    encoder
    frame_processor
    muxer
    sink
)

if(ENABLE_VIEWER)
    target_compile_definitions(modules PRIVATE ENABLE_VIEWER)
    target_link_libraries(modules PUBLIC viewer)
endif()

target_link_libraries(modules PRIVATE
    ipc_compile_options
)
