add_library(frame_processor STATIC
    frame_processor_manager.c
    osd_processor.c
)

target_include_directories(frame_processor PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(frame_processor PUBLIC
    core
)

target_link_libraries(frame_processor PRIVATE
    ipc_compile_options
)
