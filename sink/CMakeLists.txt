add_library(sink STATIC
    frame_sink_manager.c
    snapshot_jpeg_sink.c
)

target_include_directories(sink PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(sink PUBLIC
    core
)

target_link_libraries(sink PRIVATE
    PkgConfig::FFMPEG
    ipc_compile_options
)
