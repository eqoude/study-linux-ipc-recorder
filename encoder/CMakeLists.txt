add_library(encoder STATIC
    encoder_manager.c
    fake_encoder.c
    h264_ffmpeg_encoder.c
)

target_include_directories(encoder PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(encoder PUBLIC
    core
)

target_link_libraries(encoder PRIVATE
    PkgConfig::FFMPEG
    ipc_compile_options
)
