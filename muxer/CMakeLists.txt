add_library(muxer STATIC
    muxer_manager.c
    mp4_muxer.c
    rtsp_muxer.c
    segment_muxer.c
    fake_muxer.c
)

target_include_directories(muxer PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(muxer PUBLIC
    core
)

target_link_libraries(muxer PRIVATE
    PkgConfig::FFMPEG
    ipc_compile_options
)
